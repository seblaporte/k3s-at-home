#!/usr/bin/env python3
"""Génère le script d'une étape de la migration VLAN, à exécuter sur le routeur.

    generate.py <étape> [--devices -]

Les appareils (devices.sops.yaml déchiffré en JSON) arrivent sur l'entrée standard,
jamais par un fichier : `sops -d --output-type json … | generate.py vlans --devices -`.

Le script produit (sh BusyBox) accepte les sous-commandes :
  snapshot  copie de ce que l'étape modifie hors uci (fichiers, état des services)
  stage     modifications uci en attente (uci changes) et fichiers préparés dans $W/new
  install   met les fichiers préparés en place, active les services
  reload    recharge les services concernés
Le commit uci et le retour arrière sont faits par guard.sh.

Toutes les sections créées sont nommées et préfixées par "vlan_" (ou portent le nom
de l'interface) : chaque exécution les supprime puis les recrée, l'étape est donc
rejouable. Les sections existantes (guest, VPN, pbr…) ne sont touchées que là où le
plan le prévoit.
"""

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
PLAN = HERE.parent / "plan.yaml"

STEPS = {
    "rescue-port": "Port de secours : VLAN 1 non tagué sur le port de secours",
    "vlans": "VLAN 4 à 7, zones et règles, DHCP, zone du VLAN 3, réservations",
    "dhcp": "Réservations DHCP seules (après modification de devices.sops.yaml)",
    "mdns": "Relais mDNS Avahi (installe avahi-nodbus-daemon)",
    "radius": "FreeRADIUS : liste MAC → VLAN, service activé et démarré",
    "cleanup": "Nettoyage : pool DHCP du VLAN 1 en .150–.250, réservation obsolète",
}
NEEDS_DEVICES = {"vlans", "dhcp", "radius"}

NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
MAC_RE = re.compile(r"^([0-9a-f]{2}:){5}[0-9a-f]{2}$")

AUTHORIZE = "/etc/freeradius3/mods-config/files/authorize"
AVAHI_CONF = "/etc/avahi/avahi-daemon.conf"


def q(value):
    """Valeur entre apostrophes pour sh."""
    return "'" + str(value).replace("'", "'\\''") + "'"


def sec(name):
    return re.sub(r"[^a-z0-9_]", "_", name.lower())


class Script:
    def __init__(self):
        self.stage = []
        self.install = []
        self.reload = []
        self.files = []  # chemins sauvegardés par snapshot
        self.services = []  # services dont l'état (enabled) est sauvegardé

    def uci(self, *args):
        self.stage.append("uci " + " ".join(args))

    def set(self, path, value):
        self.uci("set", f"{path}={q(value)}")

    def add_list(self, path, value):
        self.uci("add_list", f"{path}={q(value)}")

    def section(self, config, sid, stype, /, **options):
        self.set(f"{config}.{sid}", stype)
        for key, value in options.items():
            if isinstance(value, list):
                for item in value:
                    self.add_list(f"{config}.{sid}.{key}", item)
            else:
                self.set(f"{config}.{sid}.{key}", value)

    def file(self, path, content):
        """Fichier préparé dans $W/new, mis en place par install."""
        tmp = "$W/new/" + path.strip("/").replace("/", "__")
        marker = "VLAN_EOF"
        assert marker not in content
        self.stage.append(f"cat > {tmp} <<'{marker}'\n{content}{marker}")
        self.install.append(f"cp {tmp} {q(path)}")
        self.files.append(path)

    def render(self, step):
        def block(lines):
            # Pas d'indentation : elle casserait les heredocs
            return "\n".join(lines) or ":"

        files = " ".join(q(f) for f in self.files)
        services = " ".join(self.services)
        return f"""#!/bin/sh
# Généré par network/openwrt/generate.py, étape « {step} ». Ne pas éditer.
set -e
W=/tmp/vlan
FILES="{files}"
SERVICES="{services}"

# Sections d'un type dont une option vaut une valeur (sections anonymes comprises)
find_sec() {{
  uci -X show "$1" | sed -n "s/^$1\\.\\([^.=]*\\)\\.$3='$4'\\$/\\1/p" | while read -r s; do
    if [ "$(uci -q get "$1.$s")" = "$2" ]; then echo "$s"; fi
  done
}}

# Supprime les sections créées par ce script (préfixe vlan_)
drop_vlan_sections() {{
  for s in $(uci -X show "$1" | sed -n "s/^$1\\.\\(vlan_[a-z0-9_]*\\)=.*/\\1/p"); do
    uci delete "$1.$s"
  done
}}

snapshot() {{
  mkdir -p $W/rollback/files
  : > $W/rollback/files.list
  for f in $FILES; do
    b=$W/rollback/files/$(echo "$f" | tr / _)
    if [ -e "$f" ]; then cp -p "$f" "$b"; echo "$f $b" >> $W/rollback/files.list
    else echo "$f -" >> $W/rollback/files.list; fi
  done
  : > $W/rollback/services.list
  for s in $SERVICES; do
    if [ -x /etc/init.d/$s ] && /etc/init.d/$s enabled; then e=1; else e=0; fi
    echo "$s $e" >> $W/rollback/services.list
  done
}}

stage() {{
  mkdir -p $W/new
{block(self.stage)}
}}

install() {{
{block(self.install)}
}}

reload() {{
{block(self.reload)}
}}

case "$1" in
  snapshot|stage|install|reload) "$1" ;;
  *) echo "usage: $0 snapshot|stage|install|reload" >&2; exit 2 ;;
esac
"""


def load_devices(stream):
    raw = json.load(stream)
    devices = (raw.get("data") or {}).get("devices") or []
    names, macs, ips = set(), set(), set()
    for d in devices:
        name, mac = d.get("name", ""), str(d.get("mac", "")).lower()
        if not NAME_RE.match(name):
            sys.exit(f"nom invalide : {name!r}")
        if not MAC_RE.match(mac):
            sys.exit(f"{name} : MAC invalide")
        if name in names or mac in macs:
            sys.exit(f"{name} : nom ou MAC en double")
        for key in ("ip", "ip_lan"):
            ip = d.get(key)
            if ip and ip in ips:
                sys.exit(f"{name} : IP {ip} en double")
            if ip:
                ips.add(ip)
        if d.get("ip") and d["ip"].split(".")[2] != str(d["vlan"]):
            sys.exit(f"{name} : {d['ip']} hors du VLAN {d['vlan']}")
        if d.get("ip_lan") and not d["ip_lan"].startswith("192.168.1."):
            sys.exit(f"{name} : ip_lan hors du VLAN 1")
        if d.get("wired") and d.get("radius"):
            sys.exit(f"{name} : un appareil filaire ne passe pas par RADIUS")
        names.add(name)
        macs.add(mac)
        d["mac"] = mac
    return devices


def rescue_port(s, plan):
    # Section bridge-vlan du VLAN 1 (anonyme aujourd'hui) : trunk tagué + port de secours
    s.stage.append(
        "S=$(find_sec network bridge-vlan vlan 1)\n"
        '[ -n "$S" ] || { echo "bridge-vlan du VLAN 1 introuvable" >&2; exit 1; }\n'
        "uci delete network.$S.ports"
    )
    s.uci("add_list", f"network.$S.ports={q(plan['trunk_port'] + ':t')}")
    s.uci("add_list", f"network.$S.ports={q(plan['rescue_port'] + ':u*')}")
    s.reload.append("/etc/init.d/network reload")


def expand(ips, plan):
    out = []
    for ip in ips:
        out.extend(plan["k3s_nodes"] if ip == "k3s_nodes" else [ip])
    return out


def reservations(s, devices):
    s.stage.append("drop_vlan_sections dhcp")
    for d in devices:
        if d.get("ip"):
            # Sans dns=1 : le nom n'est publié qu'avec un bail réel, pas avant que
            # l'appareil ait rejoint son VLAN
            s.section("dhcp", f"vlan_h_{sec(d['name'])}", "host",
                      name=d["name"], mac=d["mac"], ip=d["ip"])
        if d.get("ip_lan"):
            # Même MAC, autre sous-réseau : dnsmasq retient la réservation du VLAN où
            # l'appareil se trouve. Pas de nom, il est porté par la réservation cible.
            s.section("dhcp", f"vlan_hl_{sec(d['name'])}", "host",
                      mac=d["mac"], ip=d["ip_lan"])


def vlans(s, plan, devices):
    adguard = plan["adguard"]

    # --- network : bridge-vlan et interfaces (VLAN 3 existant : rien à changer)
    for v in plan["vlans"]:
        if v.get("existing"):
            continue
        vid, name = v["id"], v["name"]
        s.section("network", f"vlan_br{vid}", "bridge-vlan",
                  device="br-lan", vlan=str(vid), ports=[plan["trunk_port"] + ":t"])
        s.section("network", name, "interface",
                  proto="static", device=f"br-lan.{vid}",
                  ipaddr=f"192.168.{vid}.1", netmask="255.255.255.0")

    # --- dhcp : pool .150–.250, DNS AdGuard puis le routeur du VLAN
    for v in plan["vlans"]:
        vid, name = v["id"], v["name"]
        s.section("dhcp", name, "dhcp", interface=name, start="150", limit="101",
                  leasetime="12h")
        s.stage.append(f"uci -q delete dhcp.{name}.dhcp_option || true")
        s.add_list(f"dhcp.{name}.dhcp_option", f"6,{adguard},192.168.{vid}.1")
    reservations(s, devices)

    # --- firewall
    s.stage.append("drop_vlan_sections firewall")
    for v in plan["vlans"]:
        name = v["name"]
        s.section("firewall", f"vlan_zone_{name}", "zone", name=name, network=[name],
                  input=v["input"], output="ACCEPT", forward="REJECT")

    # Zone guest existante : accès au routeur fermé, sauf DHCP et DNS
    s.stage.append(
        "G=$(find_sec firewall zone name guest)\n"
        '[ -n "$G" ] || { echo "zone guest introuvable" >&2; exit 1; }'
    )
    s.set("firewall.$G.input", "REJECT")

    def input_rules(zone, icmp=True, mdns=False):
        z = sec(zone)
        s.section("firewall", f"vlan_in_{z}_dhcp", "rule", name=f"VLAN {zone} DHCP",
                  src=zone, proto="udp", dest_port="67", family="ipv4", target="ACCEPT")
        s.section("firewall", f"vlan_in_{z}_dns", "rule", name=f"VLAN {zone} DNS",
                  src=zone, proto=["tcp", "udp"], dest_port="53", target="ACCEPT")
        if icmp:
            s.section("firewall", f"vlan_in_{z}_icmp", "rule", name=f"VLAN {zone} ICMP",
                      src=zone, proto="icmp", icmp_type=["echo-request"],
                      family="ipv4", target="ACCEPT")
        if mdns:
            s.section("firewall", f"vlan_in_{z}_mdns", "rule", name=f"VLAN {zone} mDNS",
                      src=zone, proto="udp", dest_port="5353",
                      dest_ip="224.0.0.251", family="ipv4", target="ACCEPT")

    input_rules("guest", icmp=False)
    for v in plan["vlans"]:
        if v["input"] == "REJECT":
            input_rules(v["name"], mdns=v.get("mdns", False))

    for src, dest in plan["forwardings"]:
        s.section("firewall", f"vlan_fwd_{sec(src)}_{sec(dest)}", "forwarding",
                  src=src, dest=dest)

    # DNS AdGuard pour chaque VLAN sans accès complet au VLAN 1
    full_lan = {src for src, dest in plan["forwardings"] if dest == "lan"}
    for v in plan["vlans"]:
        if v["name"] in full_lan:
            continue
        s.section("firewall", f"vlan_fw_{sec(v['name'])}_dns", "rule",
                  name=f"VLAN {v['name']} vers AdGuard", src=v["name"], dest="lan",
                  dest_ip=[adguard], proto=["tcp", "udp"], dest_port="53",
                  family="ipv4", target="ACCEPT")
    for r in plan["rules"]:
        s.section("firewall", f"vlan_fw_{sec(r['name'])}", "rule",
                  name=f"VLAN {r['name']}", src=r["src"], dest="lan",
                  dest_ip=expand(r["dest_ip"], plan), proto=[r["proto"]],
                  dest_port=" ".join(str(p) for p in r["ports"]),
                  family="ipv4", target="ACCEPT")

    # Redirection « VPN alias NVR » supprimée (le VPN n'a pas accès au VLAN 5)
    s.stage.append(
        "for R in $(find_sec firewall redirect name 'VPN alias NVR'); do\n"
        "  uci delete firewall.$R\n"
        "done"
    )

    s.reload += [
        "/etc/init.d/network reload",
        "/etc/init.d/firewall reload",
        "/etc/init.d/dnsmasq restart",
    ]


def dhcp_only(s, devices):
    reservations(s, devices)
    s.reload.append("/etc/init.d/dnsmasq restart")


def mdns(s, plan):
    allow = ",".join(plan["mdns_interfaces"])
    s.file(AVAHI_CONF, f"""# Généré par network/openwrt/generate.py (relais mDNS entre VLAN)
[server]
use-ipv4=yes
use-ipv6=no
allow-interfaces={allow}
ratelimit-interval-usec=1000000
ratelimit-burst=1000

[wide-area]
enable-wide-area=no

[publish]
disable-publishing=yes

[reflector]
enable-reflector=yes
reflect-ipv=no

[rlimits]
rlimit-core=0
rlimit-data=4194304
rlimit-fsize=0
rlimit-nofile=300
rlimit-stack=4194304
rlimit-nproc=3
""")
    s.services.append("avahi-daemon")
    # Paquet installé à l'application seulement (pas pendant un plan), avant la config
    s.install.insert(0, (
        "if ! opkg list-installed | grep -q '^avahi-nodbus-daemon '; then\n"
        "  opkg update >/dev/null && opkg install avahi-nodbus-daemon\n"
        "fi"
    ))
    s.install.append("/etc/init.d/avahi-daemon enable")
    s.reload.append("/etc/init.d/avahi-daemon restart")


def radius(s, devices):
    moved = [d for d in devices if d.get("radius") and not d.get("wired")]
    lines = [
        "# Généré par network/openwrt/generate.py depuis network/devices.sops.yaml.",
        "# Une MAC listée reçoit son VLAN ; une MAC inconnue reste dans le VLAN par",
        "# défaut du SSID (règle DEFAULT sans attribut).",
        "",
    ]
    for d in moved:
        lines += [
            f"# {d['name']}",
            f'"{d["mac"]}"\tAuth-Type := Accept',
            "\tTunnel-Type = VLAN,",
            "\tTunnel-Medium-Type = IEEE-802,",
            f'\tTunnel-Private-Group-Id = "{d["vlan"]}"',
            "",
        ]
    lines += ["DEFAULT\tAuth-Type := Accept", ""]
    s.file(AUTHORIZE, "\n".join(lines))
    s.stage.append(
        "grep -q '^client unifi-aps' /etc/freeradius3/clients.conf || "
        "{ echo 'client unifi-aps absent de clients.conf' >&2; exit 1; }"
    )
    s.services.append("radiusd")
    # Contrôle de la configuration avant de remplacer le service en cours
    s.install += ["radiusd -C >/dev/null", "/etc/init.d/radiusd enable"]
    s.reload.append("/etc/init.d/radiusd restart")


def cleanup(s):
    s.set("dhcp.lan.start", "150")
    s.set("dhcp.lan.limit", "101")
    s.stage.append(
        "for H in $(find_sec dhcp host name artillery-3d-printer); do\n"
        "  uci delete dhcp.$H\n"
        "done"
    )
    s.reload.append("/etc/init.d/dnsmasq restart")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("step", choices=STEPS)
    parser.add_argument("--devices", help="'-' : JSON des appareils sur stdin")
    parser.add_argument("--list", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    plan = yaml.safe_load(PLAN.read_text())
    devices = []
    if args.step in NEEDS_DEVICES:
        if args.devices != "-":
            sys.exit(f"l'étape {args.step} a besoin des appareils : --devices -")
        devices = load_devices(sys.stdin)

    s = Script()
    if args.step == "rescue-port":
        rescue_port(s, plan)
    elif args.step == "vlans":
        vlans(s, plan, devices)
    elif args.step == "dhcp":
        dhcp_only(s, devices)
    elif args.step == "mdns":
        mdns(s, plan)
    elif args.step == "radius":
        radius(s, devices)
    elif args.step == "cleanup":
        cleanup(s)
    sys.stdout.write(s.render(args.step))


if __name__ == "__main__":
    main()
