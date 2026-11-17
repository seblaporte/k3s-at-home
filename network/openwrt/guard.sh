#!/bin/sh
# Applique l'étape préparée dans /tmp/vlan, avec retour automatique.
# Lancé détaché par vlan.sh (start-stop-daemon), pour survivre à la coupure de la
# session SSH pendant le rechargement du réseau.
#
#   guard.sh <délai en secondes>
#
# Après application, attend /tmp/vlan/confirmed pendant le délai. Sans confirmation
# (ou avec /tmp/vlan/rollback-now), restaure /etc/config, les fichiers et l'état des
# services sauvegardés par « step.sh snapshot », puis redémarre les services.

W=/tmp/vlan
TIMEOUT=${1:-180}
exec >> $W/guard.log 2>&1

state() { echo "$1" > $W/state; echo "$(date '+%T') $1"; }

rollback() {
  state rolling-back
  cp -p $W/rollback/config/* /etc/config/
  while read -r f b; do
    if [ "$b" = "-" ]; then rm -f "$f"; else cp -p "$b" "$f"; fi
  done < $W/rollback/files.list
  while read -r s e; do
    [ -x /etc/init.d/$s ] || continue
    if [ "$e" = 1 ]; then /etc/init.d/$s enable; /etc/init.d/$s restart
    else /etc/init.d/$s stop; /etc/init.d/$s disable; fi
  done < $W/rollback/services.list
  /etc/init.d/network restart
  /etc/init.d/firewall restart
  /etc/init.d/dnsmasq restart
  state rolled-back
}

state applying
if ! { uci commit && sh $W/step.sh install && sh $W/step.sh reload; }; then
  echo "échec de l'application"
  rollback
  exit 1
fi
state applied

i=0
while [ $i -lt "$TIMEOUT" ]; do
  if [ -f $W/confirmed ]; then state confirmed; exit 0; fi
  if [ -f $W/rollback-now ]; then break; fi
  sleep 1
  i=$((i + 1))
done
rollback
