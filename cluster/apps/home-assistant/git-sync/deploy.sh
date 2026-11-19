#!/bin/bash
# git-sync exechook: deploys the synced commit of seblaporte/home-assistant-config into /config.
#
# Runs in the git-sync sidecar, with the synced worktree as working directory and
# $GITSYNC_HASH set. git-sync calls it again at startup and retries it on failure
# (--exechook-backoff), so it must be idempotent.
#
# Git is the only source of truth ("strict" mode): local changes in /config (UI edits)
# are saved as a patch, reported, then overwritten.
#   1. /config already at this commit and clean -> nothing to do
#   2. HA API unreachable -> exit 1, git-sync retries later (files untouched)
#   3. git reset --hard on the new commit (fetched from the local git-sync worktree)
#   4. check_config invalid -> roll back, notify, never retry this commit
#   5. valid -> homeassistant.reload_all, notify when a restart is required
set -euo pipefail

CONFIG=${CONFIG:-/config}
HA_URL=${HA_URL:-http://127.0.0.1:8123}
STATE=$CONFIG/.git/git-sync # inside .git: never tracked, survives pod restarts
TOKEN=$(cat "${HA_TOKEN_FILE:-/etc/git-secret/ha-token}")
TARGET=${GITSYNC_HASH:?GITSYNC_HASH not set}
SHORT=${TARGET:0:7}

g() { git -C "$CONFIG" "$@"; }

log() { echo "[deploy $SHORT] $*"; }

json_str() {
  local s=$1
  s=${s//\\/\\\\}
  s=${s//\"/\\\"}
  s=${s//$'\n'/\\n}
  s=${s//$'\t'/ }
  printf '"%s"' "$s"
}

api() { # api <path> [json body] -> prints the response body
  local body=${2:-}
  [ -n "$body" ] || body='{}'
  curl -fsS -m 120 -X POST \
    -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
    -d "$body" "$HA_URL/api/$1"
}

notify() { # notify <id suffix> <title> <message>
  api services/persistent_notification/create \
    "{\"notification_id\":\"git_sync_$1\",\"title\":$(json_str "$2"),\"message\":$(json_str "$3")}" >/dev/null ||
    log "notification failed: $2"
}

# The Synology indexer drops @eaDir metadata folders everywhere, .git included, where git
# reads them as broken refs/objects ("bad object refs/@eaDir/..."). They are never git data.
shopt -s globstar nullglob
for d in "$CONFIG"/.git/**/@eaDir; do rm -rf "$d"; done
shopt -u globstar nullglob

mkdir -p "$STATE"

if [ "$(cat "$STATE/rejected" 2>/dev/null || true)" = "$TARGET" ]; then
  log "commit already rejected by check_config, waiting for a new one"
  exit 0
fi

CURRENT=$(g rev-parse HEAD)
DIRTY=$(g status --porcelain)
if [ "$CURRENT" = "$TARGET" ] && [ -z "$DIRTY" ]; then
  log "/config already up to date"
  exit 0
fi

# Do not touch the files while Home Assistant cannot validate them
if ! api config/core/check_config >/dev/null; then
  log "Home Assistant API unreachable, retrying later"
  exit 1
fi

g fetch -q "$PWD" HEAD
[ "$(g rev-parse FETCH_HEAD)" = "$TARGET" ] || {
  log "fetched commit differs from GITSYNC_HASH"
  exit 1
}

if [ -n "$DIRTY" ]; then
  ts=$(date -u +%Y%m%dT%H%M%SZ)
  g diff HEAD >"$STATE/local-$ts.patch"
  printf '%s\n' "$DIRTY" >"$STATE/local-$ts.status"
  log "local changes saved to .git/git-sync/local-$ts.patch: $(printf '%s' "$DIRTY" | tr '\n' ' ')"
  notify local "Modifications locales écrasées" \
    "Des fichiers de /config différaient de Git et ont été remplacés par le commit $SHORT (Git est la seule source de vérité).
Fichiers : $(printf '%s' "$DIRTY" | sed 's/^...//' | tr '\n' ' ')
Sauvegarde : /config/.git/git-sync/local-$ts.patch (fichiers non suivis laissés en place)."
fi

CHANGED=$(g diff --name-only "$CURRENT" "$TARGET")
g reset -q --hard "$TARGET"

RESULT=$(api config/core/check_config) || RESULT="check_config call failed"
if ! printf '%s' "$RESULT" | grep -Eq '"result" *: *"valid"'; then
  g reset -q --hard "$CURRENT"
  echo "$TARGET" >"$STATE/rejected"
  log "check_config rejected the commit, rolled back to ${CURRENT:0:7}: $RESULT"
  notify error "Configuration Git refusée" \
    "Le commit $SHORT ne passe pas check_config : /config est resté sur ${CURRENT:0:7}.
Détail : $RESULT"
  exit 0
fi

api services/homeassistant/reload_all >/dev/null || log "reload_all failed"
log "deployed (was ${CURRENT:0:7}), changed: $(printf '%s' "$CHANGED" | tr '\n' ' ')"

if printf '%s\n' "$CHANGED" | grep -Eq '^(configuration\.yaml|packages/)'; then
  notify restart "Redémarrage requis" \
    "Le commit $SHORT modifie configuration.yaml ou packages/ : rechargé partiellement, un redémarrage complet de Home Assistant est nécessaire pour l'appliquer entièrement."
fi
