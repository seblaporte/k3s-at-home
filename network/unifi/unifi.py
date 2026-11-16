#!/usr/bin/env python3
"""Configure le contrôleur UniFi pour la migration VLAN (voir network/README.md).

    unifi.py status                         réseaux, SSID, profils RADIUS (lecture seule)
    unifi.py networks        [--apply]      réseaux VLAN 4 à 7 (« VLAN only »)
    unifi.py radius-profile  [--apply]      profil openwrt-radius (VLAN dynamiques)
    unifi.py mac-auth SSID on|off [--apply] RADIUS MAC Authentication sur un SSID
    unifi.py ssid-vlan SSID VLAN [--apply]  VLAN par défaut d'un SSID (1 = Default)
    unifi.py kick MAC                       déconnecte un client Wi-Fi (il se reconnecte
                                            aussitôt et repasse par RADIUS)

Sans --apply, rien n'est envoyé : la commande affiche ce qui changerait. Les noms de
SSID sont passés en argument (ils ne sont pas versionnés).

Identifiants : compte local UNIFI_USER (défaut claude-automation), mot de passe dans le
trousseau macOS (service UNIFI_KEYCHAIN, défaut unifi-local). Le secret
RADIUS est lu dans le trousseau (service openwrt-radius) seulement pour créer le profil.
"""

import argparse
import http.cookiejar
import json
import os
import ssl
import subprocess
import sys
import urllib.error
import urllib.request

URL = os.environ.get("UNIFI_URL", "https://192.168.1.22:8443")
SITE = os.environ.get("UNIFI_SITE", "default")
USER = os.environ.get("UNIFI_USER", "claude-automation")
KEYCHAIN = os.environ.get("UNIFI_KEYCHAIN", "unifi-local")
RADIUS_KEYCHAIN = os.environ.get("RADIUS_KEYCHAIN", "openwrt-radius")

RADIUS_PROFILE = "openwrt-radius"
RADIUS_SERVER = {"ip": "192.168.1.1", "port": 1812}

# Réseaux « VLAN only » : le routeur (OpenWRT) assure le DHCP et le routage
NETWORKS = [
    (4, "Multimedia"),
    (5, "Security"),
    (6, "IoT"),
    (7, "Car"),
]


def keychain(service):
    out = subprocess.run(
        ["security", "find-generic-password", "-s", service, "-w"],
        capture_output=True, text=True,
    )
    if out.returncode != 0:
        sys.exit(f"trousseau : entrée « {service} » introuvable")
    return out.stdout.rstrip("\n")


class Controller:
    def __init__(self):
        # Certificat autosigné du contrôleur
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ctx),
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()),
        )
        self.call("POST", "/api/login",
                  {"username": USER, "password": keychain(KEYCHAIN)}, site=False)

    def call(self, method, path, body=None, site=True):
        url = URL + (f"/api/s/{SITE}" if site else "") + path
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with self.opener.open(req, timeout=15) as resp:
                payload = json.load(resp)
        except urllib.error.HTTPError as e:
            sys.exit(f"{method} {path} : HTTP {e.code} {e.read()[:300]!r}")
        if payload.get("meta", {}).get("rc") != "ok":
            sys.exit(f"{method} {path} : {payload.get('meta')}")
        return payload.get("data", [])

    def networks(self):
        return self.call("GET", "/rest/networkconf")

    def wlans(self):
        return self.call("GET", "/rest/wlanconf")

    def radius_profiles(self):
        return self.call("GET", "/rest/radiusprofile")

    def wlan(self, ssid):
        found = [w for w in self.wlans() if w.get("name") == ssid]
        if len(found) != 1:
            sys.exit(f"SSID « {ssid} » introuvable (unifi.py status)")
        return found[0]


def network_vlan(n):
    if n.get("purpose") == "corporate" and not n.get("vlan_enabled"):
        return 1
    return int(n["vlan"]) if n.get("vlan_enabled") and n.get("vlan") else None


def show_change(what, before, after):
    keys = sorted(set(before) | set(after))
    diff = {k: (before.get(k), after.get(k)) for k in keys if before.get(k) != after.get(k)}
    if not diff:
        print(f"{what} : déjà conforme")
        return False
    print(f"{what} :")
    for k, (old, new) in diff.items():
        print(f"  {k}: {old!r} → {new!r}")
    return True


def cmd_status(c, _args):
    nets = c.networks()
    by_id = {n["_id"]: n for n in nets}
    profiles = {p["_id"]: p for p in c.radius_profiles()}
    print("Réseaux :")
    for n in sorted(nets, key=lambda n: network_vlan(n) or 0):
        print(f"  VLAN {network_vlan(n)!s:4} {n.get('name')}  ({n.get('purpose')})")
    print("Profils RADIUS :")
    for p in profiles.values():
        servers = ", ".join(f"{s.get('ip')}:{s.get('port')}" for s in p.get("auth_servers", []))
        print(f"  {p.get('name')}  serveurs={servers or '-'}  "
              f"vlan_enabled={p.get('vlan_enabled')}  vlan_wlan_mode={p.get('vlan_wlan_mode')}")
    print("SSID :")
    for w in c.wlans():
        net = by_id.get(w.get("networkconf_id"), {})
        prof = profiles.get(w.get("radiusprofile_id"), {})
        print(f"  {w.get('name')}  VLAN {network_vlan(net) if net else '?'} ({net.get('name')})  "
              f"mac_auth={w.get('radius_mac_auth_enabled', False)}  "
              f"format={w.get('radius_macacl_format')}  profil={prof.get('name')}  "
              f"actif={w.get('enabled')}")


def cmd_networks(c, args):
    existing = {network_vlan(n): n for n in c.networks()}
    for vid, name in NETWORKS:
        want = {"name": name, "purpose": "vlan-only", "vlan_enabled": True, "vlan": vid,
                "enabled": True}
        if vid in existing:
            n = existing[vid]
            before = {k: n.get(k) for k in want}
            if n.get("vlan") is not None:
                before["vlan"] = int(n["vlan"])
            if show_change(f"réseau VLAN {vid}", before, want) and args.apply:
                c.call("PUT", f"/rest/networkconf/{n['_id']}", want)
        else:
            show_change(f"réseau VLAN {vid} (nouveau)", {}, want)
            if args.apply:
                c.call("POST", "/rest/networkconf", want)
    if not args.apply:
        print("(rien d'envoyé : --apply pour appliquer)")


def cmd_radius_profile(c, args):
    found = [p for p in c.radius_profiles() if p.get("name") == RADIUS_PROFILE]
    # vlan_enabled concerne le filaire : laissé tel quel (False lors du test validé)
    want = {"name": RADIUS_PROFILE, "vlan_wlan_mode": "optional",
            "accounting_enabled": False}
    if found:
        p = found[0]
        servers = [{"ip": s.get("ip"), "port": s.get("port")} for s in p.get("auth_servers", [])]
        before = {k: p.get(k) for k in want}
        before["auth_servers"] = servers
        if show_change(f"profil {RADIUS_PROFILE}", before,
                       {**want, "auth_servers": [RADIUS_SERVER]}) and args.apply:
            # Le secret existant est conservé : seuls ip/port sont réécrits
            secret = next((s.get("x_secret") for s in p.get("auth_servers", [])), None)
            secret = secret or keychain(RADIUS_KEYCHAIN)
            c.call("PUT", f"/rest/radiusprofile/{p['_id']}",
                   {**want, "auth_servers": [{**RADIUS_SERVER, "x_secret": secret}]})
    else:
        show_change(f"profil {RADIUS_PROFILE} (nouveau)", {},
                    {**want, "auth_servers": [RADIUS_SERVER]})
        if args.apply:
            c.call("POST", "/rest/radiusprofile",
                   {**want, "auth_servers": [{**RADIUS_SERVER,
                                              "x_secret": keychain(RADIUS_KEYCHAIN)}]})
    if not args.apply:
        print("(rien d'envoyé : --apply pour appliquer)")


def cmd_mac_auth(c, args):
    w = c.wlan(args.ssid)
    if args.state == "on":
        prof = [p for p in c.radius_profiles() if p.get("name") == RADIUS_PROFILE]
        if not prof:
            sys.exit(f"profil {RADIUS_PROFILE} absent : unifi.py radius-profile --apply")
        want = {"radius_mac_auth_enabled": True, "radius_macacl_format": "colon_lower",
                "radiusprofile_id": prof[0]["_id"]}
    else:
        want = {"radius_mac_auth_enabled": False}
    before = {k: w.get(k) for k in want}
    if show_change(f"SSID {args.ssid}", before, want) and args.apply:
        c.call("PUT", f"/rest/wlanconf/{w['_id']}", want)
    if not args.apply:
        print("(rien d'envoyé : --apply pour appliquer)")


def cmd_ssid_vlan(c, args):
    w = c.wlan(args.ssid)
    nets = [n for n in c.networks() if network_vlan(n) == args.vlan]
    if len(nets) != 1:
        sys.exit(f"réseau du VLAN {args.vlan} introuvable ou ambigu (unifi.py networks)")
    by_id = {n["_id"]: n for n in c.networks()}
    old = by_id.get(w.get("networkconf_id"), {})
    if show_change(f"SSID {args.ssid}",
                   {"vlan": network_vlan(old) if old else None, "network": old.get("name")},
                   {"vlan": args.vlan, "network": nets[0].get("name")}) and args.apply:
        c.call("PUT", f"/rest/wlanconf/{w['_id']}", {"networkconf_id": nets[0]["_id"]})
    if not args.apply:
        print("(rien d'envoyé : --apply pour appliquer)")


def cmd_kick(c, args):
    c.call("POST", "/cmd/stamgr", {"cmd": "kick-sta", "mac": args.mac.lower()})
    print("client déconnecté")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    for name in ("networks", "radius-profile"):
        sub.add_parser(name).add_argument("--apply", action="store_true")
    p = sub.add_parser("mac-auth")
    p.add_argument("ssid")
    p.add_argument("state", choices=["on", "off"])
    p.add_argument("--apply", action="store_true")
    p = sub.add_parser("ssid-vlan")
    p.add_argument("ssid")
    p.add_argument("vlan", type=int)
    p.add_argument("--apply", action="store_true")
    sub.add_parser("kick").add_argument("mac")
    args = parser.parse_args()

    c = Controller()
    {
        "status": cmd_status,
        "networks": cmd_networks,
        "radius-profile": cmd_radius_profile,
        "mac-auth": cmd_mac_auth,
        "ssid-vlan": cmd_ssid_vlan,
        "kick": cmd_kick,
    }[args.cmd](c, args)


if __name__ == "__main__":
    main()
