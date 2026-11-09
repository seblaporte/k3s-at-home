#!/usr/bin/env bash
# Environnement ESPHome identique au cluster k3s, dans Docker (appelé par `task esphome:*` et la CI).
#
# Même image (tag lu dans helm-release.yaml, mis à jour par Renovate) et même organisation
# que le pod :
#   /config      lecture seule : devices/*.yaml + secrets.yaml
#   /components  lecture seule : composants locaux (components/<nom>/)
#   /data        ESPHOME_DATA_DIR : builds, état, composants externes clonés
#   /cache       toolchains PlatformIO (volume Docker persistant)
#
# Variables : DEVICE=<nom> (sans .yaml)  IP=<adresse>  PORT=/dev/cu.…  DUMMY=1  ESPHOME_VERSION=…

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=$ROOT/.work
DEVICE=${DEVICE:-}
IP=${IP:-}
PORT=${PORT:-}
DUMMY=${DUMMY:-}
# Volume Docker nommé en local ; chemin d'un dossier en CI (mis en cache par actions/cache)
PIO_CACHE=${PIO_CACHE:-esphome-cache}

version() {
  if [ -n "${ESPHOME_VERSION:-}" ]; then echo "$ESPHOME_VERSION"; return; fi
  local v
  v=$(sed -n 's/^ *tag: *"\{0,1\}\([0-9][0-9.]*\)"\{0,1\} *$/\1/p' "$ROOT/helm-release.yaml" | head -1)
  [ -n "$v" ] || { echo "Version d'ESPHome introuvable dans helm-release.yaml" >&2; exit 1; }
  echo "$v"
}

devices() {
  local f
  for f in "$ROOT"/devices/*.yaml; do basename "$f" .yaml; done
}

need_device() {
  [ -n "$DEVICE" ] || { echo "Préciser DEVICE=<nom> parmi : $(devices | tr '\n' ' ')" >&2; exit 1; }
  [ -f "$ROOT/devices/$DEVICE.yaml" ] || { echo "devices/$DEVICE.yaml introuvable" >&2; exit 1; }
}

need_ip() {
  [ -n "$IP" ] || { echo "Préciser IP=… (le mDNS ne traverse pas Docker Desktop)" >&2; exit 1; }
}

secrets() {
  local tmp
  tmp=$(mktemp "$ROOT/secrets.yaml.XXXXXX")
  sops -d --extract '["stringData"]["secrets.yaml"]' "$ROOT/secret.sops.yaml" > "$tmp"
  chmod 600 "$tmp" && mv "$tmp" "$ROOT/secrets.yaml"
  echo "secrets.yaml déchiffré (ignoré par Git)"
}

# DUMMY=1 : secrets factices (CI, ou validation sans clé PGP)
stage() {
  if [ -z "$DUMMY" ]; then secrets; fi
  rm -rf "$WORK/config" && mkdir -p "$WORK/config" "$WORK/data"
  cp "$ROOT"/devices/*.yaml "$ROOT/devices/.gitignore" "$WORK/config/"
  if [ -n "$DUMMY" ]; then
    python3 "$ROOT/scripts/dummy-secrets.py" > "$WORK/config/secrets.yaml"
  else
    cp "$ROOT/secrets.yaml" "$WORK/config/"
  fi
}

docker_run() {
  local tty=()
  if [ -t 0 ] && [ -t 1 ]; then tty=(-it); fi
  # shellcheck disable=SC2086 # DOCKER_OPTS est une liste d'options sans espace
  docker run --rm ${tty[@]+"${tty[@]}"} \
    -v "$WORK/config:/config:ro" \
    -v "$ROOT/components:/components:ro" \
    -v "$WORK/data:/data" -e ESPHOME_DATA_DIR=/data \
    -v "$PIO_CACHE:/cache" \
    ${DOCKER_OPTS:-} "esphome/esphome:$(version)" "$@"
}

cmd_validate() {
  stage
  echo "ESPHome $(version)"
  local fail=0 d out
  for d in ${DEVICE:-$(devices)}; do
    if out=$(docker_run config "/config/$d.yaml" 2>&1); then
      echo "  OK    $d"
    else
      echo "  ÉCHEC $d"
      echo "$out" | grep -E "ERROR|Failed|^\[" | head -5 | sed 's/^/        /'
      fail=1
    fi
  done
  exit $fail
}

cmd_compile() { need_device; stage; docker_run compile "/config/$DEVICE.yaml"; }
cmd_upload() { need_device; need_ip; stage; docker_run upload "/config/$DEVICE.yaml" --device "$IP"; }
cmd_run() { need_device; need_ip; stage; docker_run compile "/config/$DEVICE.yaml"; docker_run upload "/config/$DEVICE.yaml" --device "$IP"; }
cmd_logs() { need_device; need_ip; stage; docker_run logs "/config/$DEVICE.yaml" --device "$IP"; }
cmd_dashboard() { stage; DOCKER_OPTS="-p 6052:6052" docker_run dashboard /config; }

cmd_flash_usb() {
  [ -n "$PORT" ] || { echo "Préciser PORT=… (ls /dev/cu.*)" >&2; exit 1; }
  cmd_compile
  [ -x "$WORK/venv/bin/esptool" ] || { python3 -m venv "$WORK/venv" && "$WORK/venv/bin/pip" install -q esptool; }
  local bin
  bin=$(find "$WORK/data/build/$DEVICE/.pioenvs" -name firmware.factory.bin 2>/dev/null | head -1)
  [ -n "$bin" ] || { echo "firmware.factory.bin introuvable pour $DEVICE" >&2; exit 1; }
  echo "Flash de $bin sur $PORT"
  "$WORK/venv/bin/esptool" --port "$PORT" write-flash 0x0 "$bin"
}

cmd_check() {
  python3 "$ROOT/scripts/gen-kustomization.py" --check
  kubectl kustomize "$ROOT" > /dev/null && echo "kustomize build OK"
}

case "${1:-}" in
  version) version ;;
  secrets) secrets ;;
  validate) cmd_validate ;;
  compile) cmd_compile ;;
  upload) cmd_upload ;;
  run) cmd_run ;;
  logs) cmd_logs ;;
  flash-usb) cmd_flash_usb ;;
  dashboard) cmd_dashboard ;;
  kustomization) python3 "$ROOT/scripts/gen-kustomization.py" ;;
  check) cmd_check ;;
  clean) rm -rf "$WORK" "$ROOT/secrets.yaml" ;;
  *) echo "Usage : $0 {version|secrets|validate|compile|upload|run|logs|flash-usb|dashboard|kustomization|check|clean}" >&2; exit 2 ;;
esac
