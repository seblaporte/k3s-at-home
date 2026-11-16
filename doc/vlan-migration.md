# Migration VLAN du réseau domestique

Document de reprise : décisions prises, plan d'adressage, règles inter-VLAN et méthode de
migration. À lire en premier par toute session (humaine ou Claude Code) qui travaille sur
ce chantier.

## Matériel et décisions

| Élément             | Choix                                                                                    |
| ------------------- | ---------------------------------------------------------------------------------------- |
| Routeur             | Ubiquiti EdgeRouter X sous OpenWRT (DSA `bridge-vlan`), accès SSH par clé                |
| Switch              | D-Link DGS-1100-16V2 (web-smart, sans CLI, **non PoE**, non flashable sous OpenWRT)      |
| Bornes Wi-Fi        | UniFi U6-LR et U6-Lite, contrôleur dans le cluster (`192.168.1.22`, v10.0.162)           |
| Wi-Fi               | **Les 3 SSID existants sont gardés** (nom et clé), VLAN par SSID + VLAN par MAC (RADIUS) |
| Inter-VLAN          | **Routage + pare-feu** (politique par défaut : refus), pas de NAT                        |
| Configuration       | Routeur et UniFi pilotés par script depuis le Mac, switch configuré à la main guidée     |
| Contrainte majeure  | **Aucune IP du cluster ne change** : le VLAN 1 reste le LAN actuel                       |
| Contrainte majeure  | **Aucun objet à réappairer** : ni SSID ni clé Wi-Fi ne changent pour les appareils       |
| Numérotation        | **Les VLAN 2 (guest) et 3 (home) existants sont conservés**, le reste s'y ajoute         |
| Accès des automates | Clé SSH dédiée (`~/.ssh/id_rsa_openwrt`), compte local UniFi `claude-automation`         |

> Les identifiants (mot de passe UniFi, clés SSH) ne vont jamais dans ce dépôt : trousseau
> macOS en local, SOPS si un secret doit être versionné. `uci show network` affiche les
> clés WireGuard en clair : toujours filtrer (`grep -v -E 'private_key|preshared_key'`).

Points à traiter côté matériel :

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

Switch (export du 2026-11-16) : les 16 ports sont en trunk, VLAN 1 natif, VLAN 2 à
4094 autorisés tagués ; VLAN déclarés : 1, 2 (Guest), 3 (Home). SNMP désactivé, interface
web en HTTP.

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

| Appareil                                    | VLAN | IP actuelle             | IP cible       |
| ------------------------------------------- | ---- | ----------------------- | -------------- |
| Switch D-Link                               | 1    | `1.2`                   | `1.2`          |
| Borne U6-LR                                 | 1    | `1.140`                 | `1.4`          |
| Borne U6-Lite                               | 1    | `1.141`                 | `1.5`          |
| NAS Synology                                | 1    | `1.10`                  | `1.10`         |
| Mac mini (filaire, Frigate)                 | 1    | `1.90`                  | `1.90`         |
| Nœuds k3s (×6)                              | 1    | `1.100`–`.105`          | inchangées     |
| Invités                                     | 2    | `2.x`                   | DHCP           |
| Mac, iPhone, iPad, Apple Watch de la maison | 3    | `1.x`                   | DHCP           |
| PC portables (×2)                           | 3    | `1.133`, `1.218`        | DHCP           |
| Mac mini (Wi-Fi)                            | 3    | `1.162`                 | DHCP           |
| Apple TV (concentrateur HomeKit)            | 3    | `1.187`                 | `3.10`         |
| Nest Audio (×2)                             | 4    | `1.205`, `1.228`        | `4.10`, `4.11` |
| Google Home Mini (×2)                       | 4    | `1.126`, `1.165`        | `4.12`, `4.13` |
| Chromecast Audio                            | 4    | `1.245`                 | `4.14`         |
| Chromecast Google TV, Nest Hub              | 4    | `1.154`, `1.238`        | `4.15`, `4.16` |
| NVR                                         | 5    | `1.3`                   | `5.10`         |
| Caméra                                      | 5    | `1.109`                 | `5.20`         |
| Caméra                                      | 5    | `1.113`                 | `5.21`         |
| ESPHome (×9, `cluster/apps/esphome`)        | 6    | `1.40`–`.50`            | `6.40`–`.50`   |
| Objet cloud (module Tuya)                   | 6    | `1.209`                 | `6.60`         |
| Shelly                                      | 6    | `1.45`                  | `6.45`         |
| Aspirateurs (×3)                            | 6    | `1.121`, `.183`, `.239` | `6.61`–`.63`   |
| OctoPrint                                   | 6    | `1.214`                 | `6.64`         |
| Imprimante Epson                            | 6    | `1.193`                 | `6.65`         |
| Passerelle domotique (filaire)              | 6    | `1.122`                 | `6.66`         |
| Objet domotique                             | 6    | `1.178`                 | `6.67`         |
| Objet Tuya piloté en local                  | 6    | `1.180`                 | `6.68`         |
| Objets cloud (électroménager, santé, etc.)  | 6    | `1.x`, `2.172`          | DHCP           |
| Autres objets du SSID IoT (station météo…)  | 6    | `1.x`                   | DHCP           |
| Véhicule                                    | 7    | `1.115`                 | `7.10`         |
| Raspberry musique du véhicule               | 7    | `1.221`                 | `7.11`         |
| AirPort Express du véhicule                 | 7    | `1.176`                 | `7.12`         |

Notes :

- Les ESPHome gardent leur dernier octet, mais **`manual_ip` est retiré** de chaque fichier
  de `cluster/apps/esphome/devices/` au profit d'une réservation DHCP par VLAN (même MAC,
  `1.4x` puis `6.4x`). Un seul flash OTA, fait pendant qu'ils sont encore dans le VLAN 1 :
  au passage dans le VLAN 6, ils prennent seuls leur nouvelle IP.
- Toutes les IP cibles fixes sont des réservations DHCP sur le routeur : rien n'est saisi
  sur les appareils. Seules les intégrations qui visent une IP sont mises à jour :
  Home Assistant (`esphome`, `shelly`, `tuya_local`, `dreame_vacuum`, `octoprint`, `ipp`,
  `androidtv_remote` et les deux intégrations du NVR), go2rtc (`configmap.yaml`, flux RTSP
  du NVR), Frigate (Mac mini) et Scrypted.
- Absents pendant l'audit (SSID à constater à la migration) : la tondeuse, le véhicule,
  l'imprimante.
- Appareils ignorés : quatre modules Espressif hors ESPHome (`1.31`, `.33`, `.34`, `.36`).
- À nettoyer : la réservation DHCP `artillery-3d-printer` (`1.65`, jamais vue) et les
  anciens clients du contrôleur UniFi, l'intégration `ipp` obsolète de Home Assistant
  (`1.254`, absent).

## Wi-Fi : SSID existants, VLAN par SSID et par appareil

Réappairer les objets (objets cloud, robots, électroménager…) est exclu : un objet ne
se reconfigure que si son SSID ou sa clé change. Les **trois SSID actuels sont donc gardés
tels quels** (nom, clé, WPA2) ; seul le VLAN derrière change, de deux façons :

1. un **VLAN par défaut par SSID** (`networkconf_id` du SSID) ;
2. une **exception par appareil** par **RADIUS MAC Authentication** : à la connexion, la
   borne envoie la MAC de l'appareil à FreeRADIUS (sur le routeur), qui répond le VLAN
   (`Tunnel-Private-Group-Id`). Une MAC inconnue reste dans le VLAN par défaut du SSID.
   L'appareil ne voit rien d'autre qu'une nouvelle IP.

La « Virtual Network Override » du contrôleur (VLAN par client) **n'est pas utilisable** :
elle exige une passerelle UniFi, l'option n'apparaît pas avec un routeur tiers.

| SSID      | VLAN par défaut (cible) | Exceptions par appareil (RADIUS)                                                |
| --------- | ----------------------- | ------------------------------------------------------------------------------- |
| IoT       | 6 (iot)                 | aucune (tous les appareils vus à l'audit sont de l'IoT)                         |
| Principal | 3 (home)                | Google / Chromecast → 4, caméras → 5, OctoPrint et imprimante → 6, véhicule → 7 |
| Invités   | 2 (guest, inchangé)     | un objet cloud → 6                                                              |

Les appareils Apple restent dans le VLAN par défaut du SSID principal : **l'adresse Wi-Fi
privée peut rester active**. Les exceptions ne visent que des objets non Apple, dont la
MAC est fixe. Une MAC privée Apple est propre à chaque SSID.

Un SSID peut être ajouté plus tard pour regrouper autrement, sans toucher aux existants.

### Mise en œuvre RADIUS

- **Routeur** : paquets `freeradius3-default` et `freeradius3-utils` (installés, service
  arrêté et désactivé depuis le test). Client `unifi-aps` (`192.168.1.0/24`, secret
  partagé dans le trousseau macOS, service `openwrt-radius`). Correspondances MAC → VLAN
  dans `mods-config/files/authorize`, une entrée `Auth-Type := Accept` par appareil
  avant une règle `DEFAULT` sans VLAN.
- **UniFi** : profil RADIUS `openwrt-radius` (serveur `192.168.1.1:1812`, prise en charge
  des VLAN pour le Wi-Fi : `vlan_wlan_mode: "optional"`). Sur chaque SSID concerné :
  `radius_mac_auth_enabled`, `radius_macacl_format: "colon_lower"`, `radiusprofile_id`.
- **Liste MAC → VLAN** : adresses MAC, donc dans un fichier SOPS édité à la main, rendu
  vers le routeur par le script.

Risques et parades :

- Une fois activé sur un SSID, **FreeRADIUS devient nécessaire pour s'y connecter**. Il
  tourne sur le routeur (si le routeur tombe, le réseau tombe de toute façon) et procd le
  relance. Sonde de supervision : CronJob `radius-probe` (requête MAC Auth toutes les
  5 minutes) et alerte `RadiusDown`. Secours : désactiver
  « RADIUS MAC Authentication » sur le SSID dans UniFi, tout redevient comme avant.
- Ce n'est pas une mesure de sécurité : la clé WPA2 reste la seule barrière, une MAC se
  falsifie (mais seulement par quelqu'un qui connaît déjà la clé).

> Test validé le 2026-11-15 sur le SSID invités : un iPhone associé au VLAN 1 dans
> FreeRADIUS a reçu une IP `192.168.1.x` sans changer de SSID. Piège rencontré : le SSID
> doit utiliser le profil `openwrt-radius` ; avec le profil « Default » (sans prise en
> charge des VLAN), la borne interroge le routeur mais ignore le VLAN de la réponse.

## Règles inter-VLAN

Validées le 2026-11-15. Politique par défaut : refus entre VLAN, retour des flux par suivi
d'état.

Les services du cluster qui initient des flux sortent avec **l'IP de leur nœud**, pas avec
leur IP MetalLB : Home Assistant et Music Assistant (`hostNetwork`, nœud 4), Scrypted
(`hostNetwork`, nœud 3), go2rtc (SNAT du nœud). Les règles visent donc `192.168.1.100` à
`.105`, et les IP MetalLB ne servent qu'aux flux entrants.

Accès au routeur (zone → routeur) :

| Zone                                           | Politique | Autorisé                                     |
| ---------------------------------------------- | --------- | -------------------------------------------- |
| lan (1), home (3)                              | accepter  | Tout, dont l'administration (SSH, LuCI)      |
| guest (2)                                      | refuser   | DHCP, DNS (aujourd'hui « accepter » : fermé) |
| multimedia (4), security (5), iot (6), car (7) | refuser   | DHCP, DNS (secours), ICMP                    |
| vpn (`wg0`)                                    | accepter  | Inchangé                                     |

Flux entre zones :

| Source → destination            | Autorisé                                                                 |
| ------------------------------- | ------------------------------------------------------------------------ |
| VLAN 1 → VLAN 3, 4, 5, 6, 7     | Tout (zone serveur de confiance : cluster, Mac mini avec Frigate)        |
| VLAN 3 → VLAN 1, 4, 6           | Tout (pas d'accès au VLAN 5)                                             |
| VPN `wg0` → VLAN 1, 4, 6        | Mêmes droits que le VLAN 3 ; alias NAS, DNS, Traefik conservés           |
| VLAN 3, 4, 5, 6, 7 → `1.23`     | DNS AdGuard (53 TCP/UDP)                                                 |
| VLAN 4 → `1.100`–`.105`         | Music Assistant en `hostNetwork` : 8095, 8097                            |
| VLAN 4 → `1.34`, `1.29`         | Music Assistant (8095, 8097), Jellyfin (8096) pour le cast               |
| VLAN 6 → `1.21`, `1.38`         | Mosquitto (1883, 8883)                                                   |
| VLAN 6 → `1.32`, `1.100`–`.105` | Home Assistant (80, 8123) : objets qui poussent vers lui (Shelly, météo) |
| VLAN 7 → `1.33`                 | Navidrome (4533)                                                         |
| VLAN 1, 3, 4, 6, 7 → Internet   | Autorisé (VLAN 4 : DNS Google codés en dur ; VLAN 6 : à restreindre)     |
| VLAN 5 → Internet               | Autorisé : accès à distance par le cloud du fabricant (NVR et caméras)   |
| VLAN 2 → Internet               | Uniquement via le VPN des invités (inchangé)                             |

Relais mDNS (Avahi sur OpenWRT) sur les VLAN 1, 3, 4 et 6 : Cast et AirPlay (Music
Assistant, iPhone), HomeKit (Apple TV, Home Assistant), imprimante. Avahi relaie entre
toutes les interfaces listées, pas couple par couple : la découverte est visible entre ces
quatre VLAN, le pare-feu filtre le trafic. Pas de relais pour les VLAN 2, 5 et 7.

AdGuard est un point de défaillance : les baux DHCP des nouveaux VLAN donnent AdGuard puis
le routeur du VLAN. Le VLAN 2 garde `8.8.8.8` (via le VPN).

À mettre à jour avec le NVR (`192.168.5.10`) : les flux RTSP de go2rtc et de Frigate. La
redirection « VPN alias NVR » (`10.50.1.3`) est **supprimée** : 0 paquet en 100 jours,
l'accès à distance passe par le cloud du fabricant, et le VPN n'a pas accès au VLAN 5.

## Switch DGS-1100-16V2

Le routeur envoie déjà les VLAN 1, 2 et 3 tagués sur `eth1` : vérifier dans l'export de la
configuration du switch le mode VLAN actuel (« 802.1Q » attendu) et l'appartenance du
port 1. Pour chaque port : appartenance au VLAN (untagged / tagged / non membre) et PVID.

| Port      | Rôle                                      | Non tagué (PVID) | Tagués           |
| --------- | ----------------------------------------- | ---------------- | ---------------- |
| 1         | Trunk vers le routeur                     | à constater      | 1 à 7            |
| 2, 3      | Bornes U6-LR / U6-Lite (injecteurs PoE)   | 1                | 2, 3, 4, 5, 6, 7 |
| 4 à 8     | Raspberry du cluster                      | 1                | aucun            |
| 9         | NAS Synology                              | 1                | aucun            |
| 10        | NVR                                       | 5                | aucun            |
| 11        | Raspberry du véhicule (si filaire)        | 7                | aucun            |
| à repérer | Passerelle domotique, Shelly (si filaire) | 6                | aucun            |
| 12 à 16   | Mac mini (VLAN 1), libres / à décider     | à choisir        | aucun            |

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
- créer la zone de pare-feu manquante du VLAN 3 `home` ;
- supprimer la redirection « VPN alias NVR » ;
- zone `guest` : passer l'accès au routeur (`input`) de « accepter » à « refuser », avec
  des règles DHCP et DNS (le transfert vers le VPN des invités ne change pas).

Paquets : `avahi-nodbus-daemon` (reflector mDNS, sans D-Bus) à installer ; FreeRADIUS
déjà installé (voir « Mise en œuvre RADIUS »).

Précisions apportées en phase 3 (`network/plan.yaml`) :

- les zones des VLAN 4 et 6 acceptent aussi le mDNS (UDP 5353) vers le routeur, sans
  quoi Avahi ne reçoit rien de ces VLAN ;
- le DNS AdGuard est ouvert aux VLAN 4 à 7 ; le VLAN 3 l'a déjà par son accès complet
  au VLAN 1 ;
- le VLAN 3 passe au pool `.150`–`.250` et distribue AdGuard puis `192.168.3.1` ;
- `uci` : sections créées nommées `vlan_*` (réservations, zones, règles), interfaces
  `multimedia`, `security`, `iot`, `car` ;
- l'adresse du NVR est une variable Flux (`NVR_ADDR` dans `cluster-settings.yaml`) :
  go2rtc bascule sur `192.168.5.10` en changeant cette seule valeur ;
- le fichier `authorize` de FreeRADIUS laissé par le test renvoie le VLAN 2 pour toute
  MAC (`DEFAULT … Tunnel-Private-Group-Id = "2"`) : il est remplacé à l'étape 4.5 par la
  liste générée, dont la règle `DEFAULT` ne fixe aucun VLAN.

## Méthode de migration

1. ~~**Audit en lecture seule**~~ : fait le 2026-11-15 (voir « État constaté » et
   « Inventaire »).
2. ~~**Plan définitif**~~ : fait le 2026-11-15 (adressage, matrice de pare-feu, exceptions
   par appareil, accès Internet, accès WireGuard).
3. ~~**Configuration versionnée**~~ : préparée le 2026-11-16 dans `network/` (voir
   `network/README.md`, qui donne la commande de chaque étape de la phase 4) : script
   OpenWRT avec retour automatique (`network/openwrt/`), liste MAC → VLAN et
   réservations dans `network/devices.sops.yaml` (à créer depuis le modèle), script
   UniFi (`network/unifi/unifi.py`), checklist du switch, ESPHome sans `manual_ip`,
   intégrations à mettre à jour (`network/integrations.md`), sonde du port RADIUS
   (`cluster/apps/networking/radius-probe/`, branchée à l'étape 4.5). Rien n'est
   appliqué à ce stade.
4. **Migration par étapes**, chacune validée avant la suivante. Avancement du
   2026-11-16 : 4.1 à 4.5 faites (plus NVR et Shelly passés en DHCP, ESPHome reflashés
   sans `manual_ip`, toujours en VLAN 1) ; 4.6 commencée : `home-mini-1` en VLAN 4
   (`192.168.4.12`, Cast vu depuis le VLAN 1), **lecture Music Assistant à tester**
   avant de déplacer les autres appareils multimédia. Sonde RADIUS : à brancher à la
   fusion dans `main` (créer `radius-probe/secret.sops.yaml`).
   1. routeur : port de secours `eth2` ;
   2. routeur : VLAN 4 à 7, zones, DHCP, réservations, zone du VLAN 3 (sans effet, aucun
      client) ;
   3. switch : VLAN 4 à 7 tagués sur le trunk et les bornes, enregistrés après contrôle ;
   4. UniFi : réseaux VLAN 4 à 7 (les SSID restent en VLAN 1 par défaut) ;
   5. FreeRADIUS démarré et activé, liste MAC → VLAN vide, puis « RADIUS MAC
      Authentication » activée SSID par SSID (aucun effet : les MAC inconnues restent dans
      le VLAN par défaut) ;
   6. appareils déplacés **un par un** en ajoutant leur MAC dans FreeRADIUS, puis
      reconnexion, groupe par groupe (multimedia avec Music Assistant, puis home, iot,
      security avec le NVR filaire, car). Retour arrière : retirer la MAC ;
   7. quand un SSID est vide de VLAN 1, basculer son VLAN par défaut et retirer les
      entrées devenues inutiles.
5. **Vérifications après chaque étape** : ping, DNS, MQTT, lecture Music Assistant, flux
   caméras, accès web, objets cloud en ligne, Internet des invités via le VPN, accès
   WireGuard.
6. **Nettoyage** : pool DHCP du VLAN 1 resserré à `.150`–`.250`, bornes en `.4` / `.5`,
   réservation obsolète supprimée, `doc/network.puml` mis à jour (numérotation des VLAN,
   VLAN 7, suppression de `pi-gateway`).

Garde-fous : accès physique (câble) au routeur et au switch pendant les opérations,
confirmation avant tout changement pouvant couper le réseau, migration à un moment où la
maison n'utilise pas le réseau.

## Questions ouvertes

- Que fait exactement le Raspberry du véhicule (Bluetooth, Wi-Fi, serveur de musique) ?
- Le Shelly et la passerelle domotique sont-ils filaires (port du switch à repérer) ?
- SSID des appareils absents pendant l'audit (tondeuse, véhicule, imprimante, santé).
