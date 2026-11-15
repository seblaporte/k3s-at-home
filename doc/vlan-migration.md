# Migration VLAN du réseau domestique

Document de reprise : décisions prises, plan d'adressage, règles inter-VLAN et méthode de
migration. À lire en premier par toute session (humaine ou Claude Code) qui travaille sur
ce chantier.

## Matériel et décisions

| Élément             | Choix                                                                                |
| ------------------- | ------------------------------------------------------------------------------------ |
| Routeur             | Ubiquiti EdgeRouter X sous OpenWRT (DSA `bridge-vlan`), accès SSH par clé            |
| Switch              | D-Link DGS-1100-16V2 (web-smart, sans CLI, **non PoE**, non flashable sous OpenWRT)  |
| Bornes Wi-Fi        | UniFi U6-LR et U6-Lite, contrôleur dans le cluster (`192.168.1.22`, v10.0.162)       |
| Wi-Fi               | **Un seul SSID, WPA2, une clé PPSK par VLAN** (pas un SSID par VLAN)                 |
| Inter-VLAN          | **Routage + pare-feu** (politique par défaut : refus), pas de NAT                    |
| Configuration       | Routeur et UniFi pilotés par script depuis le Mac, switch configuré à la main guidée |
| Contrainte majeure  | **Aucune IP du cluster ne change** : le VLAN 1 reste le LAN actuel                   |
| Numérotation        | **Les VLAN 2 (guest) et 3 (home) existants sont conservés**, le reste s'y ajoute     |
| Accès des automates | Clé SSH dédiée (`~/.ssh/id_ed25519_openwrt`), compte local UniFi `claude-automation` |

> Les identifiants (mot de passe UniFi, clés SSH) ne vont jamais dans ce dépôt : trousseau
> macOS en local, SOPS si un secret doit être versionné. `uci show network` affiche les
> clés WireGuard en clair : toujours filtrer (`grep -v -E 'private_key|preshared_key'`).

Points à traiter côté matériel :

- Le DGS-1100-16V2 n'est pas PoE : les U6-LR et U6-Lite ont besoin d'injecteurs PoE
  (802.3af), ou d'un switch PoE. Un switch UniFi PoE géré par le même contrôleur est
  l'alternative recommandée si un remplacement est envisagé.
- Un seul lien trunk relie le routeur au switch (`eth1`) : tout le trafic inter-VLAN passe
  par le CPU de l'EdgeRouter X.
- Un port du routeur doit rester en VLAN 1 non tagué comme **port de secours** (câble).
  **Il n'existe pas aujourd'hui** : à créer en premier (voir « État constaté »).

## État constaté (audit du 2026-11-15)

Routeur :

- `br-lan` regroupe `eth1` à `eth4`, mais seul `eth1` est membre des VLAN, **tagué pour
  les VLAN 1, 2 et 3** : le switch reçoit donc déjà le VLAN 1 tagué. `eth2` à `eth4` ne
  sont membres d'aucun VLAN.
- VLAN 2 `guest` (`192.168.2.0/24`, DNS `8.8.8.8`) : SSID invités, zone `guest`, sortie
  Internet **uniquement par un VPN commercial** (politique `pbr` sur `192.168.2.0/24`,
  transfert de la zone `guest` vers la zone du VPN).
- VLAN 3 `home` (`192.168.3.0/24`) : interface et DHCP, **aucune zone de pare-feu**, aucun
  client.
- WireGuard `wg0` (`192.168.10.0/24`) : zone `vpn` vers `lan` et `wan`, alias
  `10.50.1.x` → NAS, routeur, NVR (`192.168.1.3`) et Traefik. `pbr` « LAN via WAN » ne
  concerne que `192.168.1.0/24`. Indépendant des VLAN 2 et 3.
- DHCP du VLAN 1 : pool `.100` à `.249` (`start=100`, `limit=150`), qui chevauche les
  réservations des nœuds k3s. DNS distribués : AdGuard `192.168.1.23` puis `8.8.8.8`.
  Option 66 (boot réseau des Raspberry sur le TFTP du NAS).
- Redirections WAN Traefik 80/443 présentes mais désactivées (tunnel Cloudflare).
- Flash : environ 200 Mo libres. `avahi-daemon` non installé.

UniFi : trois SSID WPA2 (deux sur le LAN dont un pour l'IoT, un invités sur le VLAN 2),
aucune clé PPSK. Réseaux déclarés : Default, Guest (VLAN 2), Home (VLAN 3).

Écarts avec le plan initial :

- Bornes en `.140` et `.141` (DHCP dynamique), pas en `.2` à `.9`.
- NVR en `192.168.1.3`, dans le VLAN 1.
- Les ESPHome ont une IP fixe (`manual_ip`) en `192.168.1.40` à `.50` ; `.40` est dans le
  pool MetalLB (`.20` à `.40`).
- Le `pi-gateway` en `192.168.1.20` de `doc/network.puml` n'existe plus : `.20` est
  InfluxDB.

## VLAN et plan d'adressage

L'ID du VLAN est le 3ᵉ octet du sous-réseau. Le VLAN 1 est le LAN existant. Les VLAN 2 et 3
existent déjà et gardent leur numéro (le VPN des invités dépend de
`192.168.2.0/24`). Les équipements réseau (switch, bornes) restent dans le VLAN 1, avec le
contrôleur UniFi, ce qui évite l'adoption L3.

| VLAN | Nom        | Sous-réseau    | Contenu                                                   |
| ---- | ---------- | -------------- | --------------------------------------------------------- |
| 1    | server     | 192.168.1.0/24 | Cluster, NAS, Mac mini serveur, switch, bornes, MetalLB   |
| 2    | guest      | 192.168.2.0/24 | Invités de passage (Internet via VPN, existant)           |
| 3    | home       | 192.168.3.0/24 | Mac, iPhone, iPad, Apple Watch, PC, Apple TV (existant)   |
| 4    | multimedia | 192.168.4.0/24 | Nest, Nest Hub, Chromecast                                |
| 5    | security   | 192.168.5.0/24 | NVR (`.10`), caméras                                      |
| 6    | iot        | 192.168.6.0/24 | ESPHome, aspirateurs, OctoPrint, imprimante, objets cloud |
| 7    | car        | 192.168.7.0/24 | Véhicule et ses accessoires (Raspberry, AirPort Express)  |

Convention commune : `.1` passerelle, `.2` à `.149` adresses fixes ou réservations DHCP,
`.150` à `.250` pool DHCP dynamique.

Plages du VLAN 1 (cible) :

| Plage         | Usage                                                   |
| ------------- | ------------------------------------------------------- |
| `.1`          | Routeur                                                 |
| `.2` à `.9`   | Switch (`.2`), bornes U6-LR (`.4`) et U6-Lite (`.5`)    |
| `.10`         | NAS Synology                                            |
| `.20` à `.40` | Pool MetalLB (`cluster-settings.yaml`)                  |
| `.90`         | Mac mini (serveur, filaire)                             |
| `.100`–`.105` | Nœuds k3s                                               |
| `.150`–`.250` | DHCP dynamique (aujourd'hui `.100`–`.249`, à resserrer) |

## Inventaire

Validé le 2026-11-15. Les adresses MAC ne sont pas versionnées ici (dépôt public) : elles
sont dans les baux DHCP et le contrôleur UniFi, et iront dans un fichier SOPS pour les
réservations.

| Appareil                                    | VLAN | IP actuelle      | IP cible       |
| ------------------------------------------- | ---- | ---------------- | -------------- |
| Switch D-Link                               | 1    | `1.2`            | `1.2`          |
| Borne U6-LR                                 | 1    | `1.140`          | `1.4`          |
| Borne U6-Lite                               | 1    | `1.141`          | `1.5`          |
| NAS Synology                                | 1    | `1.10`           | `1.10`         |
| Mac mini (filaire)                          | 1    | `1.90`           | `1.90`         |
| Nœuds k3s (×6)                              | 1    | `1.100`–`.105`   | inchangées     |
| Invités                                     | 2    | `2.x`            | DHCP           |
| Mac, iPhone, iPad, Apple Watch de la maison | 3    | `1.x`            | DHCP           |
| PC portables (×2)                           | 3    | `1.133`, `1.218` | DHCP           |
| Mac mini (Wi-Fi)                            | 3    | `1.162`          | DHCP           |
| Apple TV (concentrateur HomeKit)            | 3    | `1.187`          | `3.10`         |
| Nest Audio (×2)                             | 4    | `1.205`, `1.228` | `4.10`, `4.11` |
| Google Home Mini (×2)                       | 4    | `1.126`, `1.165` | `4.12`, `4.13` |
| Chromecast Audio                            | 4    | `1.245`          | `4.14`         |
| Google sans nom (Nest Hub / Chromecast ?)   | 4    | `1.154`, `1.238` | `4.15`, `4.16` |
| NVR                                         | 5    | `1.3`            | `5.10`         |
| Caméra                                      | 5    | `1.109`          | `5.20`         |
| Caméra                                      | 5    | `1.113`          | `5.21`         |
| ESPHome (×9, `cluster/apps/esphome`)        | 6    | `1.40`–`.50`     | `6.40`–`.50`   |
| Objet cloud (module Tuya)                   | 6    | `1.209`          | `6.60`         |
| Aspirateurs (×2)                            | 6    | `1.121`, `1.183` | `6.61`, `6.62` |
| OctoPrint                                   | 6    | `1.214`          | `6.63`         |
| Imprimante Epson                            | 6    | `1.193`          | `6.64`         |
| Passerelle domotique                        | 6    | `1.122`          | `6.65`         |
| Objet domotique                             | 6    | `1.178`          | `6.66`         |
| Objets cloud (électroménager, santé, etc.)  | 6    | `1.x`, `2.172`   | DHCP           |
| Objets du SSID IoT non identifiés           | 6    | `1.x`            | DHCP           |
| Véhicule                                    | 7    | `1.115`          | `7.10`         |
| Raspberry musique du véhicule               | 7    | `1.221`          | `7.11`         |
| AirPort Express du véhicule                 | 7    | `1.176`          | `7.12`         |

Notes :

- Les ESPHome gardent leur dernier octet : changer `manual_ip` (IP, passerelle) dans
  chaque fichier de `cluster/apps/esphome/devices/`, puis flasher en OTA **avant** de
  basculer leur clé Wi-Fi, sinon l'appareil est injoignable.
- Appareils ignorés : quatre modules Espressif hors ESPHome (`1.31`, `.33`, `.34`, `.36`).
- À nettoyer : la réservation DHCP `artillery-3d-printer` (`1.65`, jamais vue) et les
  anciens clients du contrôleur UniFi.

## Wi-Fi : un SSID, une clé par VLAN (PPSK)

SSID unique en WPA2 Personal (le WPA3 casse certains ESP8266 et objets IoT), 2,4 GHz et
5 GHz. Une passphrase par groupe d'appareils, chacune rattachée à un VLAN dans le
contrôleur UniFi :

| Clé        | VLAN |
| ---------- | ---- |
| guest      | 2    |
| home       | 3    |
| multimedia | 4    |
| security   | 5    |
| iot        | 6    |
| car        | 7    |

Désactiver l'adresse Wi-Fi privée (MAC aléatoire) sur les appareils Apple avec une
réservation DHCP. La disponibilité du PPSK dépend de la version du contrôleur (v10.0.162) :
à vérifier au moment de l'implémentation. Les trois SSID actuels restent en place pendant
la migration.

## Règles inter-VLAN

Politique par défaut : refus entre VLAN. Le retour des flux est géré par le suivi d'état.

| Source → destination             | Autorisé                                                                  |
| -------------------------------- | ------------------------------------------------------------------------- |
| VLAN 3, 4, 5, 6 → `192.168.1.23` | DNS AdGuard (53 TCP/UDP)                                                  |
| VLAN 3 → `192.168.1.25`          | Traefik (443)                                                             |
| VLAN 4 → `192.168.1.34`          | Music Assistant : 8097 (stream), 8095 (web)                               |
| `192.168.1.100`–`.105` → VLAN 4  | Music Assistant vers les enceintes (tous ports)                           |
| VLAN 6 → `192.168.1.21`, `.38`   | Mosquitto (1883, version sécurisée)                                       |
| VLAN 6 → `192.168.1.32`          | API Home Assistant (8123)                                                 |
| VLAN 1 → VLAN 6                  | Home Assistant vers ESPHome (6053), aspirateurs, objets cloud, OctoPrint  |
| `192.168.1.30`, `.37` → VLAN 5   | Scrypted et go2rtc vers les caméras (RTSP)                                |
| VLAN 3 → VLAN 1, 4, 6            | Tout, sauf le management du routeur (SSH et LuCI depuis VLAN 3 seulement) |
| VLAN 7 → `192.168.1.33`          | Navidrome, si le Raspberry du véhicule s'en sert                          |
| VLAN 2 → Internet                | Uniquement, via le VPN (inchangé)                                         |
| VLAN 5, 6, 7 → Internet          | Refusé par défaut, à autoriser appareil par appareil                      |
| VPN `wg0` → VLAN 3 à 7           | À décider (aujourd'hui `vpn → lan` seulement)                             |

L'Apple TV (concentrateur HomeKit) est dans le VLAN 3 : la règle « VLAN 3 → VLAN 1, 4, 6 »
lui donne déjà accès à Homebridge (`192.168.1.31`) et à Home Assistant (`192.168.1.32`).
Si Music Assistant diffuse vers elle en AirPlay, ajouter `192.168.1.100`–`.105` →
`192.168.3.10`.

Relais mDNS (Avahi sur OpenWRT) : VLAN 3 ↔ 4 (AirPlay, Cast), VLAN 3 ↔ 6 (imprimante),
VLAN 3 ↔ 1 (HomeKit : Homebridge, Home Assistant), VLAN 1 ↔ 4 (Music Assistant). Le SSDP
n'est pas relayé par Avahi : ajouter les enceintes DLNA par IP fixe.

Music Assistant : en `hostNetwork` pour la découverte mDNS (les enceintes voient alors l'IP
du nœud, pas `192.168.1.34`), avec `dnsPolicy: ClusterFirstWithHostNet` et une affinité de
nœud pour garder une IP source stable.

AdGuard est un point de défaillance : mettre un DNS secondaire (routeur ou résolveur
public) dans les baux DHCP des autres VLAN. Le VLAN 2 garde `8.8.8.8` (via le VPN).

À mettre à jour avec le NVR : la redirection « VPN alias NVR » (`10.50.1.3`) pointe vers
`192.168.1.3`, à passer en `192.168.5.10`.

## Switch DGS-1100-16V2

Le routeur envoie déjà les VLAN 1, 2 et 3 tagués sur `eth1` : vérifier dans l'export de la
configuration du switch le mode VLAN actuel (« 802.1Q » attendu) et l'appartenance du
port 1. Pour chaque port : appartenance au VLAN (untagged / tagged / non membre) et PVID.

| Port    | Rôle                                    | Non tagué (PVID) | Tagués           |
| ------- | --------------------------------------- | ---------------- | ---------------- |
| 1       | Trunk vers le routeur                   | à constater      | 1 à 7            |
| 2, 3    | Bornes U6-LR / U6-Lite (injecteurs PoE) | 1                | 2, 3, 4, 5, 6, 7 |
| 4 à 8   | Raspberry du cluster                    | 1                | aucun            |
| 9       | NAS Synology                            | 1                | aucun            |
| 10      | NVR                                     | 5                | aucun            |
| 11      | Raspberry du véhicule (si filaire)      | 7                | aucun            |
| 12 à 16 | Mac mini (VLAN 1), libres / à décider   | à choisir        | aucun            |

À adapter au câblage réel. Le switch n'a pas d'API : configuration à la main guidée, en
n'**enregistrant la configuration qu'après validation** (un redémarrage annule alors une
erreur). Exporter la configuration actuelle avant de commencer.

## OpenWRT (EdgeRouter X)

DSA : une section `bridge-vlan` par VLAN sur `br-lan`, une interface `br-lan.<id>` avec son
sous-réseau, son DHCP et sa zone de pare-feu. Configuration à versionner dans le dépôt
(par exemple `network/openwrt/`) et à appliquer par script, avec :

- sauvegarde préalable (`sysupgrade -b`) ;
- application avec retour automatique si la connexion est perdue ;
- un VLAN à la fois ;
- en premier, un port de secours (`eth2`, VLAN 1 non tagué) ;
- ne pas toucher à l'interface `guest`, à la politique `pbr` des invités ni à la zone et
  à l'interface du VPN des invités ;
- créer la zone de pare-feu manquante du VLAN 3 `home`.

Paquets à prévoir : `avahi-daemon` (reflector mDNS).

## Méthode de migration

1. ~~**Audit en lecture seule**~~ : fait le 2026-11-15 (voir « État constaté » et
   « Inventaire »).
2. **Plan définitif** : adressage, règles, clés PPSK validés.
3. **Configuration versionnée** : fichiers OpenWRT, configuration UniFi, checklist du
   switch.
4. **Migration par étapes** : routeur (sans effet sur l'existant), switch, bornes, puis
   appareils par groupe (Servers et Multimedia d'abord, pour Music Assistant).
5. **Vérifications après chaque étape** : ping, DNS, MQTT, lecture Music Assistant, flux
   caméras, accès web, Internet des invités via le VPN, accès WireGuard.
6. Mettre à jour `doc/network.puml` (numérotation des VLAN, VLAN 7, suppression de
   `pi-gateway`).

Garde-fous : accès physique (câble) au routeur et au switch pendant les opérations,
confirmation avant tout changement pouvant couper le réseau, migration à un moment où la
maison n'utilise pas le réseau.

## Questions ouvertes

- Que fait exactement le Raspberry du véhicule (Bluetooth, Wi-Fi, serveur de musique) ?
- Certaines caméras passent-elles par un cloud ?
- Accès WireGuard vers les nouveaux VLAN : lesquels ?
- Music Assistant diffuse-t-il vers l'Apple TV ?
