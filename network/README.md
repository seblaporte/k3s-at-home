# Réseau : migration VLAN

Configuration versionnée de la migration décrite dans `doc/vlan-migration.md` (lire ce
document d'abord). Rien ici n'est appliqué par Flux : routeur et UniFi sont pilotés à
la main depuis le Mac, étape par étape.

| Fichier                                 | Rôle                                                               |
| --------------------------------------- | ------------------------------------------------------------------ |
| `plan.yaml`                             | VLAN, zones, transferts et règles de pare-feu (non sensible)       |
| `devices.example.yaml`                  | Modèle de `devices.sops.yaml` (MAC fictives)                       |
| `devices.sops.yaml`                     | **À créer** : MAC → VLAN, réservations DHCP, appareils déplacés    |
| `openwrt/vlan.sh`                       | Routeur : `plan`, `apply` (retour automatique), `status`, `backup` |
| `openwrt/generate.py`                   | Plan + appareils → script uci d'une étape (appelé par `vlan.sh`)   |
| `openwrt/guard.sh`                      | Sur le routeur : applique, attend la confirmation, sinon restaure  |
| `unifi/unifi.py`                        | Contrôleur UniFi : réseaux, profil RADIUS, MAC Auth, VLAN des SSID |
| `switch-checklist.md`                   | Switch D-Link, configuration manuelle guidée                       |
| `integrations.md`                       | Intégrations (Home Assistant, go2rtc, Frigate…) à mettre à jour    |
| `cluster/apps/networking/radius-probe/` | Sonde du port RADIUS (CronJob + alerte), à activer à l'étape 4.5   |

## Prérequis

- SSH vers le routeur par clé : hôte `openwrt` dans `~/.ssh/config`.
- `sops` avec la clé GPG du dépôt, `python3` avec PyYAML, `dig`.
- Trousseau macOS : `openwrt-radius` (secret RADIUS partagé) et
  `unifi-local` (mot de passe du compte local UniFi `claude-automation`).
  Autres noms : variables `RADIUS_KEYCHAIN` et `UNIFI_KEYCHAIN`.
- `devices.sops.yaml` créé depuis le modèle et chiffré (`sops -e -i`).

## Garde-fous du routeur

- `plan` prépare les modifications uci, les affiche puis les annule : rien n'est
  appliqué. À lancer avant chaque `apply`.
- `apply` refuse de démarrer si des modifications uci sont déjà en attente (LuCI). Il
  télécharge une sauvegarde `sysupgrade -b` dans `~/Backups/openwrt/` (hors du dépôt :
  elle contient les clés WireGuard), affiche les changements, demande confirmation,
  puis lance `guard.sh` détaché sur le routeur. Sans confirmation dans les 180 s
  (`CONFIRM_TIMEOUT`), ou si le routeur ne répond plus, la configuration précédente est
  restaurée seule.
- Le déchiffrement de `devices.sops.yaml` reste en mémoire (tube vers le routeur) ; le
  script généré ne vit que dans `/tmp/vlan` (RAM du routeur). `vlan.sh show` l'affiche
  avec les MAC : ne pas le coller ailleurs.
- Toutes les sections créées sont nommées `vlan_*` (ou d'après l'interface) : une étape
  est rejouable, elle supprime puis recrée ses propres sections.

Test sans le vrai fichier d'appareils :

```bash
DEVICES=network/devices.example.yaml DEVICES_PLAIN=1 network/openwrt/vlan.sh plan vlans
```

## Étapes (phase 4 de `doc/vlan-migration.md`)

| Étape | Action                                                     | Commande                                                   |
| ----- | ---------------------------------------------------------- | ---------------------------------------------------------- |
| 4.1   | Port de secours `eth2` (VLAN 1 non tagué)                  | `task network:apply STEP=rescue-port`                      |
| 4.2   | VLAN 4–7, zones, DHCP, réservations, zone du VLAN 3        | `task network:apply STEP=vlans`                            |
| 4.2   | Relais mDNS (installe `avahi-nodbus-daemon`)               | `task network:apply STEP=mdns`                             |
| —     | ESPHome : flash OTA sans `manual_ip`, encore en VLAN 1     | `integrations.md`, section ESPHome                         |
| 4.3   | Switch : VLAN 4–7 tagués sur le trunk et les bornes        | `switch-checklist.md`                                      |
| 4.4   | UniFi : réseaux VLAN 4–7                                   | `network/unifi/unifi.py networks --apply`                  |
| 4.5   | Profil RADIUS conforme                                     | `network/unifi/unifi.py radius-profile --apply`            |
| 4.5   | FreeRADIUS démarré, aucune MAC déplacée (`radius: false`)  | `task network:apply STEP=radius`                           |
| 4.5   | Sonde RADIUS : `secret.sops.yaml`, puis ajout à Flux       | voir « Sonde RADIUS » ci-dessous                           |
| 4.5   | MAC Auth, SSID par SSID (sans effet : MAC inconnues)       | `network/unifi/unifi.py mac-auth "<SSID>" on --apply`      |
| 4.6   | Déplacer un appareil : `radius: true`, puis                | `task network:apply STEP=radius` + `unifi.py kick <MAC>`   |
| 4.6   | Appareil filaire                                           | `switch-checklist.md`, étape 4.6                           |
| 4.6   | Intégrations du groupe                                     | `integrations.md`                                          |
| 4.7   | SSID vide de VLAN 1 : VLAN par défaut                      | `network/unifi/unifi.py ssid-vlan "<SSID>" <VLAN> --apply` |
| 4.7   | Entrées devenues inutiles : `radius: false`, puis          | `task network:apply STEP=radius`                           |
| 6     | Pool DHCP du VLAN 1 en `.150`–`.250`, réservation obsolète | `task network:apply STEP=cleanup`                          |

`task network:plan STEP=…` avant chaque `apply`. `task network:dhcp` (étape `dhcp`)
réapplique les seules réservations après une modification de `devices.sops.yaml`.

Retour arrière d'un appareil déplacé : `radius: false`, `apply radius`, `kick`.
Secours si FreeRADIUS tombe : `unifi.py mac-auth "<SSID>" off --apply`.

## Déplacer un appareil (étape 4.6)

1. `sops network/devices.sops.yaml` : `radius: true` sur l'appareil (un ou quelques-uns
   à la fois, groupe par groupe).
2. `network/openwrt/vlan.sh plan radius` puis `apply radius` (terminal interactif).
3. `network/unifi/unifi.py kick <MAC>` : l'appareil se reconnecte et passe par RADIUS.
4. Contrôles : bail dans le nouveau sous-réseau (`ssh openwrt cat /tmp/dhcp.leases`),
   VLAN vu par UniFi, fonction réelle (lecture, entité Home Assistant, flux caméra…),
   intégrations à mettre à jour (`integrations.md`).

Appareil filaire : port du switch (`switch-checklist.md`), pas de RADIUS.

## Pièges rencontrés

- `vlan.sh apply` demande deux confirmations au clavier : le lancer dans un vrai
  terminal, pas avec le préfixe `!` de Claude Code (il refuse alors de démarrer).
  `plan`, `status`, `show` et `unifi.py` fonctionnent avec `!`.
- Une seule application à la fois : `vlan.sh` refuse de démarrer si un `guard.sh`
  précédent tourne encore (`ssh openwrt cat /tmp/vlan/guard.log`).
- Les IP MetalLB (AdGuard `.23`…) et le NAS ne répondent pas au ping : tester le
  service (DNS, HTTP), pas l'ICMP.
- Modifier un SSID dans UniFi recharge les bornes : coupure Wi-Fi de quelques secondes
  sur tous les SSID ; certains ESP8266 mettent ~2 minutes à revenir.
- Après un déplacement, un appareil Cast peut prendre un suffixe `-1` dans son nom mDNS
  (conflit avec son ancien nom en cache, à cause du relais Avahi) : sans conséquence
  pour Music Assistant, qui identifie par UUID.
- `apply radius` ne déplace rien tout seul : un appareil garde son VLAN tant qu'il n'est
  pas reconnecté (`unifi.py kick <MAC>`, un par appareil).
- Après un déplacement, Home Assistant et Music Assistant visent l'ancienne IP
  (`Failed to connect` de `pychromecast`) pendant ~2 minutes, jusqu'à l'annonce mDNS.
- ESPHome : après un `kick`, Home Assistant ne voit la nouvelle IP qu'au redémarrage de
  l'ESP (`reboot_timeout` de `api:`, 15 min par défaut, 5 min pour les lecteurs de
  cartes). Plus rapide : bouton « Redémarrer » de l'appareil dans Home Assistant au
  lieu du `kick` (il se reconnecte via RADIUS et s'annonce au démarrage).
- Les journaux de Home Assistant et de Music Assistant sont en UTC (heure locale − 1).
- Sans `task` (`brew install go-task`) : `network/openwrt/vlan.sh <commande> <étape>`,
  et `DEVICE=… IP=… cluster/apps/esphome/scripts/esphome.sh run` pour ESPHome.

## Sonde RADIUS

`cluster/apps/networking/radius-probe/` envoie toutes les 5 minutes une requête MAC
Auth (MAC de documentation) au routeur ; l'alerte `RadiusDown` se déclenche après
15 minutes sans réponse. Elle n'est **pas encore référencée** dans
`cluster/apps/networking/kustomization.yaml` (FreeRADIUS est arrêté). À l'étape 4.5 :

1. créer `cluster/apps/networking/radius-probe/secret.sops.yaml` : Secret
   `radius-probe` (namespace `networking`), clé `RADIUS_SECRET` = secret du trousseau
   `openwrt-radius`, puis `sops -e -i` ;
2. ajouter `- radius-probe` dans `cluster/apps/networking/kustomization.yaml`.

Le pod sort avec l'IP de son nœud (`192.168.1.10x`), couverte par le client
FreeRADIUS `unifi-aps` (`192.168.1.0/24`).
