#!/usr/bin/env bash
# Pilote la migration VLAN du routeur OpenWRT depuis le Mac (voir network/README.md).
#
#   vlan.sh steps               liste des étapes
#   vlan.sh show   <étape>      script généré (contient les MAC : ne pas coller ailleurs)
#   vlan.sh plan   <étape>      modifications prévues sur le routeur, sans rien appliquer
#   vlan.sh apply  <étape>      sauvegarde, application, retour automatique sans confirmation
#   vlan.sh backup              sauvegarde sysupgrade -b vers $BACKUP_DIR
#   vlan.sh status              sections vlan_*, services RADIUS et Avahi
#
# Variables : ROUTER (hôte SSH, défaut openwrt), CONFIRM_TIMEOUT (défaut 180 s),
# BACKUP_DIR (défaut ~/Backups/openwrt, hors du dépôt : la sauvegarde contient les clés
# WireGuard), DEVICES (défaut network/devices.sops.yaml), DEVICES_PLAIN=1 pour lire un
# fichier non chiffré (test avec devices.example.yaml uniquement).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
ROUTER="${ROUTER:-openwrt}"
CONFIRM_TIMEOUT="${CONFIRM_TIMEOUT:-180}"
BACKUP_DIR="${BACKUP_DIR:-$HOME/Backups/openwrt}"
DEVICES="${DEVICES:-$ROOT/devices.sops.yaml}"
W=/tmp/vlan
NEEDS_DEVICES=" vlans dhcp radius "

die() { echo "erreur : $*" >&2; exit 1; }
r() { ssh -o ConnectTimeout=5 -o BatchMode=yes "$ROUTER" "$@"; }

devices_json() {
  [ -f "$DEVICES" ] || die "$DEVICES absent (modèle : network/devices.example.yaml)"
  if [ "${DEVICES_PLAIN:-}" = 1 ]; then
    python3 -c 'import json,sys,yaml; json.dump(yaml.safe_load(sys.stdin), sys.stdout)' < "$DEVICES"
  else
    # Déchiffré en mémoire uniquement, jamais écrit sur le disque du Mac
    sops -d --output-type json "$DEVICES"
  fi
}

generate() {
  local step="$1"
  if [[ "$NEEDS_DEVICES" == *" $step "* ]]; then
    devices_json | python3 "$HERE/generate.py" "$step" --devices -
  else
    python3 "$HERE/generate.py" "$step"
  fi
}

# Envoie le script de l'étape et guard.sh dans /tmp/vlan (RAM du routeur)
upload() {
  local step="$1"
  r "rm -rf $W && mkdir -p $W && chmod 700 $W"
  generate "$step" | r "cat > $W/step.sh"
  r "cat > $W/guard.sh" < "$HERE/guard.sh"
}

require_clean_uci() {
  local pending
  pending="$(r 'uci changes')"
  [ -z "$pending" ] || die "modifications uci déjà en attente sur le routeur :
$pending
(à valider ou annuler à la main : uci revert <config>)"
}

# Modifications uci en attente + différences des fichiers préparés
show_changes() {
  r 'uci changes'
  r "cd $W/new 2>/dev/null && for f in *; do
       [ -e \"\$f\" ] || continue
       dest=/\$(echo \"\$f\" | sed 's#__#/#g')
       echo \"--- \$dest\"
       if [ -e \"\$dest\" ]; then diff -u \"\$dest\" \"\$f\" || true; else echo '(nouveau fichier)'; cat \"\$f\"; fi
     done; true"
}

revert_all() {
  r 'for c in $(uci changes | sed -n "s/^-\{0,1\}\([a-z_]*\)\..*/\1/p" | sort -u); do uci revert "$c"; done'
}

cmd_backup() {
  mkdir -p "$BACKUP_DIR"
  chmod 700 "$BACKUP_DIR"
  local f
  f="$BACKUP_DIR/openwrt-$(date +%Y%m%d-%H%M%S).tar.gz"
  r 'sysupgrade -b /tmp/backup-vlan.tar.gz >/dev/null && cat /tmp/backup-vlan.tar.gz && rm -f /tmp/backup-vlan.tar.gz' > "$f"
  chmod 600 "$f"
  [ -s "$f" ] || die "sauvegarde vide"
  echo "sauvegarde : $f"
}

cmd_plan() {
  local step="$1"
  require_clean_uci
  upload "$step"
  r "sh $W/step.sh stage" || { revert_all; r "rm -rf $W"; die "échec de la préparation, annulée"; }
  echo "=== Étape « $step » : modifications prévues"
  show_changes
  revert_all
  r "rm -rf $W"
  echo "=== Rien n'a été appliqué (modifications annulées)."
}

cmd_apply() {
  local step="$1"
  require_clean_uci
  cmd_backup
  upload "$step"
  r "mkdir -p $W/rollback/config && cp -p /etc/config/* $W/rollback/config/ && sh $W/step.sh snapshot"
  r "sh $W/step.sh stage" || { revert_all; r "rm -rf $W"; die "échec de la préparation, annulée"; }
  echo "=== Étape « $step » : modifications à appliquer"
  show_changes
  echo
  read -r -p "Appliquer (retour automatique après ${CONFIRM_TIMEOUT}s sans confirmation) ? [o/N] " ok
  if [ "$ok" != o ]; then
    revert_all
    r "rm -rf $W"
    echo "annulé, rien n'a été appliqué"
    exit 1
  fi

  r "start-stop-daemon -S -b -x /bin/sh -- $W/guard.sh $CONFIRM_TIMEOUT"
  echo "application en cours…"
  local state="" i
  for i in $(seq 1 30); do
    sleep 2
    state="$(r "cat $W/state" 2>/dev/null || true)"
    case "$state" in applied | rolled-back) break ;; esac
  done
  case "$state" in
    applied) ;;
    rolled-back) die "échec de l'application, configuration restaurée : ssh $ROUTER cat $W/guard.log" ;;
    *) die "routeur injoignable (état : ${state:-inconnu}). Retour automatique dans ${CONFIRM_TIMEOUT}s ;
sinon câble sur le port de secours, ou restauration de la sauvegarde ci-dessus." ;;
  esac

  echo "=== Appliqué. Contrôles depuis le Mac :"
  checks || true
  echo
  read -r -p "Tout fonctionne ? Confirmer (o) ou revenir en arrière (n) [o/N] " ok
  if [ "$ok" = o ]; then
    r "touch $W/confirmed"
    sleep 2
    echo "état : $(r "cat $W/state")"
  else
    r "touch $W/rollback-now"
    echo "retour arrière demandé ; suivi : ssh $ROUTER cat $W/guard.log"
  fi
}

checks() {
  local ok=1
  ping -c1 -t2 192.168.1.1 >/dev/null && echo "  ok  ping routeur" || { echo "  KO  ping routeur"; ok=0; }
  ping -c1 -t2 192.168.1.23 >/dev/null && echo "  ok  ping AdGuard" || { echo "  KO  ping AdGuard"; ok=0; }
  dig +short +time=2 +tries=1 @192.168.1.23 one.one.one.one | grep -q . \
    && echo "  ok  DNS AdGuard" || { echo "  KO  DNS AdGuard"; ok=0; }
  dig +short +time=2 +tries=1 @192.168.1.1 one.one.one.one | grep -q . \
    && echo "  ok  DNS routeur" || { echo "  KO  DNS routeur"; ok=0; }
  r 'ping -c1 -W2 1.1.1.1 >/dev/null' && echo "  ok  Internet (routeur)" || { echo "  KO  Internet"; ok=0; }
  [ "$ok" = 1 ]
}

cmd_status() {
  r 'uci -X show network | grep -E "^network\.(vlan_|multimedia|security|iot|car|home)"
     uci -X show firewall | grep -E "^firewall\.vlan_[a-z0-9_]*=" | sed "s/=.*//" | tr "\n" " "; echo
     uci -X show dhcp | grep -c "^dhcp\.vlan_h" | sed "s/^/réservations vlan_ : /"
     for s in radiusd avahi-daemon; do
       [ -x /etc/init.d/$s ] || { echo "$s : absent"; continue; }
       /etc/init.d/$s enabled && e=activé || e=désactivé
       pidof ${s%-daemon} >/dev/null && p=démarré || p=arrêté
       echo "$s : $e, $p"
     done'
}

case "${1:-}" in
  steps) python3 - "$HERE/generate.py" <<'EOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("g", sys.argv[1]); g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)
for k, v in g.STEPS.items(): print(f"  {k:12} {v}")
EOF
  ;;
  show) generate "${2:?étape}" ;;
  plan) cmd_plan "${2:?étape}" ;;
  apply) cmd_apply "${2:?étape}" ;;
  backup) cmd_backup ;;
  status) cmd_status ;;
  *) sed -n '2,15p' "$0"; exit 2 ;;
esac
