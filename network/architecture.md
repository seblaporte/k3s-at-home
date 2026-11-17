# Architecture réseau : physique, VLAN, pare-feu

Description de référence du réseau après la migration VLAN : câblage, transport des VLAN,
affectation des clients, adressage, DNS, mDNS et règles de pare-feu entre VLAN. Relevé
sur le routeur le 2026-11-17 (`uci show`, sans clés).

- Plan déclaratif (VLAN, transferts, règles) : [`plan.yaml`](plan.yaml), traduit en `uci`
  par [`openwrt/generate.py`](openwrt/generate.py).
- Historique, décisions et inventaire : [`doc/vlan-migration.md`](../doc/vlan-migration.md).
- Ports détaillés du routeur et du switch : [`doc/network-cabling.md`](../doc/network-cabling.md).
- Aucune adresse MAC ici (dépôt public) : elles sont dans `devices.sops.yaml`.

## Vue d'ensemble

| VLAN | Zone         | Sous-réseau       | Rôle                                                           | Accès au VLAN 1           |
| ---- | ------------ | ----------------- | -------------------------------------------------------------- | ------------------------- |
| 1    | `lan`        | `192.168.1.0/24`  | Serveurs : cluster k3s, NAS, Mac mini, switch, bornes, MetalLB | —                         |
| 2    | `guest`      | `192.168.2.0/24`  | Invités de passage, Internet via un VPN commercial             | aucun                     |
| 3    | `home`       | `192.168.3.0/24`  | Mac, iPhone, iPad, PC, Apple TV                                | complet                   |
| 4    | `multimedia` | `192.168.4.0/24`  | Nest, Google Home, Chromecast                                  | Music Assistant, Jellyfin |
| 5    | `security`   | `192.168.5.0/24`  | NVR                                                            | aucun                     |
| 6    | `iot`        | `192.168.6.0/24`  | ESPHome, Shelly, aspirateurs, imprimante, objets cloud         | MQTT, Home Assistant      |
| 7    | `car`        | `192.168.7.0/24`  | Véhicule et ses accessoires (migration bloquée)                | Navidrome                 |
| —    | `vpn`        | `192.168.10.0/24` | WireGuard `wg0` (accès distant)                                | complet                   |

Dans chaque VLAN : `.1` passerelle (routeur), `.2`–`.149` adresses fixes et réservations
DHCP, `.150`–`.250` pool DHCP dynamique (VLAN 1 : resserré à la phase 6).

## Physique

Un seul lien trunk relie le routeur au switch : tout le trafic entre VLAN passe par `eth1`
et le processeur de l'EdgeRouter X. Couleur d'un appareil = son VLAN.

```mermaid
flowchart TB
    subgraph router["EdgeRouter X · OpenWRT · 192.168.1.1"]
        eth0["eth0 · WAN"]
        eth1["eth1 · trunk"]
        eth2["eth2 · secours<br/>VLAN 1 non tagué"]
    end

    subgraph sw["Switch D-Link DGS-1100-16V2 · 192.168.1.2"]
        p1["port 1"]
        p2["port 2"]
        p3["port 3"]
        p4["port 4"]
        p5["port 5"]
        p6["port 6"]
        p7["port 7"]
        p8["port 8"]
        p9["port 9"]
        p10["port 10"]
        p11["port 11"]
    end

    internet((Internet)) --- ont["ONT fibre"] --- eth0
    eth1 == "trunk : 1 natif, 2–7 tagués" === p1

    p2 == "trunk" === u6lr["Borne U6-LR"]
    p4 == "trunk" === u6lite["Borne U6-Lite"]
    p3 --- hub["Switch non administrable"]
    p5 --- epson["Imprimante"]
    p6 --- tydom["Passerelle domotique"]
    p7 --- k105["Nœud k3s .105"]
    p8 --- macmini["Mac mini · Frigate"]
    p9 --- nvr["NVR"]
    p10 --- shelly["Shelly Pro 1"]
    p11 --- k104["Nœud k3s .104"]

    hub --- nas["NAS Synology"]
    hub --- k100["Nœuds k3s .100–.103"]
    hub --- airport["AirPort Express"]
    airport -. "Wi-Fi hors UniFi" .- vehicule["Véhicule, clôture tondeuse"]

    u6lr -. "3 SSID" .- wifi(("Clients Wi-Fi"))
    u6lite -. "3 SSID" .- wifi

    classDef v1 fill:#cfe2ff,stroke:#3d6fb6,color:#000
    classDef v5 fill:#f8d7da,stroke:#b02a37,color:#000
    classDef v6 fill:#d1e7dd,stroke:#2f7d55,color:#000
    classDef v7 fill:#fff3cd,stroke:#b38600,color:#000
    class hub,k105,macmini,k104,nas,k100,u6lr,u6lite v1
    class nvr v5
    class epson,tydom,shelly v6
    class airport,vehicule v7
```

| Port du switch | Mode                                   | Appareil                                 |
| -------------- | -------------------------------------- | ---------------------------------------- |
| 1              | trunk, 1 natif, 2–7 tagués             | Routeur `eth1`                           |
| 2, 4           | trunk, 1 natif, 2–7 tagués             | Bornes U6-LR, U6-Lite (injecteurs PoE)   |
| 3, 7, 8, 11    | trunk (1 natif) : à passer en access 1 | Cluster, NAS, Mac mini                   |
| 5, 6, 10       | access VLAN 6                          | Imprimante, passerelle domotique, Shelly |
| 9              | access VLAN 5                          | NVR                                      |
| 12–16          | trunk, libres                          | —                                        |

L'AirPort Express (VLAN 7 visé) est derrière le switch non administrable du port 3,
partagé avec le NAS et le cluster : ses clients ne peuvent être déplacés ni par le port ni
par RADIUS. Voir « Questions ouvertes » de `doc/vlan-migration.md`.

## Transport des VLAN (niveau 2)

```mermaid
flowchart LR
    subgraph R["Routeur · bridge br-lan (DSA)"]
        b1["br-lan.1 · lan"]
        b2["br-lan.2 · guest"]
        b3["br-lan.3 · home"]
        b4["br-lan.4 · multimedia"]
        b5["br-lan.5 · security"]
        b6["br-lan.6 · iot"]
        b7["br-lan.7 · car"]
    end

    R == "eth1 : 1–7 tagués" ==> S["Switch"]
    R -- "eth2 : 1 non tagué" --> secours["Portable de secours"]

    S == "1 natif, 2–7 tagués" ==> AP["Bornes UniFi"]
    S -- "1 non tagué" --> srv["Cluster, NAS, Mac mini"]
    S -- "5 non tagué" --> nvr["NVR"]
    S -- "6 non tagué" --> obj["Imprimante, Tydom, Shelly"]

    AP -- "SSID principal · 3 (+ exceptions)" --> c1(("Clients"))
    AP -- "SSID IoT · 6" --> c2(("Objets"))
    AP -- "SSID invités · 2 (+ exception)" --> c3(("Invités"))
```

Le routeur envoie le VLAN 1 tagué sur `eth1` ; le switch le reçoit sur le port 1 et le
redistribue non tagué (VLAN natif). Les switchs et les bornes restent administrés dans le
VLAN 1, avec le contrôleur UniFi du cluster (`192.168.1.22`).

## Affectation Wi-Fi : VLAN par SSID et par MAC

Les trois SSID d'origine sont gardés (nom et clé) : aucun objet n'est réappairé. Chaque
SSID a un VLAN par défaut ; FreeRADIUS sur le routeur (MAC Authentication) en impose un
autre à certaines adresses MAC.

| SSID      | VLAN par défaut | Exceptions par MAC (RADIUS)                                          |
| --------- | --------------- | -------------------------------------------------------------------- |
| Principal | 3 `home`        | Google / Chromecast → 4, OctoPrint et objets cloud → 6, véhicule → 7 |
| IoT       | 6 `iot`         | aucune                                                               |
| Invités   | 2 `guest`       | un objet cloud → 6                                                   |

```mermaid
sequenceDiagram
    participant C as Client Wi-Fi
    participant AP as Borne UniFi
    participant R as FreeRADIUS (routeur)
    participant D as dnsmasq (routeur)
    C->>AP: association au SSID (WPA2)
    AP->>R: Access-Request (MAC du client)
    alt MAC listée dans devices.sops.yaml (radius: true)
        R-->>AP: Access-Accept + Tunnel-Private-Group-Id = VLAN
    else MAC inconnue
        R-->>AP: Access-Accept sans VLAN
        Note over AP: VLAN par défaut du SSID
    end
    AP->>D: DHCP dans le VLAN choisi (trame taguée)
    D-->>C: IP, passerelle, DNS du VLAN (réservation si listée)
```

- Les appareils Apple restent dans le VLAN par défaut du SSID principal : leur adresse
  Wi-Fi privée peut rester active.
- Un changement de VLAN ne s'applique qu'à la reconnexion (`unifi/unifi.py kick <MAC>`).
- Si FreeRADIUS tombe, plus aucune connexion n'est acceptée sur les SSID en MAC Auth :
  sonde `radius-probe` et alerte `RadiusDown`, secours `unifi.py mac-auth "<SSID>" off`.

## Adressage (niveau 3)

```mermaid
flowchart TB
    wan((Internet)) --- R
    vpnc(("Tunnel VPN<br/>commercial")) --- R
    wg(("WireGuard wg0<br/>192.168.10.0/24")) --- R

    R{{"EdgeRouter X<br/>routage + pare-feu"}}

    R --- v1["VLAN 1 · lan<br/>192.168.1.0/24<br/>MetalLB .20–.40 · nœuds .100–.105"]
    R --- v2["VLAN 2 · guest<br/>192.168.2.0/24"]
    R --- v3["VLAN 3 · home<br/>192.168.3.0/24"]
    R --- v4["VLAN 4 · multimedia<br/>192.168.4.0/24"]
    R --- v5["VLAN 5 · security<br/>192.168.5.0/24 · NVR .10"]
    R --- v6["VLAN 6 · iot<br/>192.168.6.0/24 · ESPHome .40–.50"]
    R --- v7["VLAN 7 · car<br/>192.168.7.0/24"]
```

Réservations notables (toutes en DHCP, rien n'est saisi sur les appareils) :

| VLAN | Plage / adresse    | Appareils                                                 |
| ---- | ------------------ | --------------------------------------------------------- |
| 1    | `.2`, `.10`, `.90` | Switch, NAS, Mac mini                                     |
| 1    | `.20`–`.40`        | IP MetalLB des services du cluster                        |
| 1    | `.100`–`.105`      | Nœuds k3s                                                 |
| 3    | `.10`              | Apple TV (concentrateur HomeKit)                          |
| 4    | `.10`–`.16`        | Nest, Google Home, Chromecast, Nest Hub                   |
| 5    | `.10`              | NVR                                                       |
| 6    | `.40`–`.50`        | ESPHome (dernier octet conservé de l'ancien `1.4x`)       |
| 6    | `.45`, `.60`–`.68` | Shelly, objets cloud, aspirateurs, OctoPrint, imprimante… |

## Pare-feu entre VLAN

Politique par défaut : **refus** entre zones (`forward=REJECT`), retour des flux autorisé
par suivi d'état (une réponse passe toujours). Une zone ne parle à une autre que par un
transfert complet (`forwarding`) ou par une règle ciblée (`rule` : IP et ports).

Les services du cluster qui initient des flux sortent avec **l'IP de leur nœud**
(`.100`–`.105`), pas leur IP MetalLB : Home Assistant et Music Assistant (`hostNetwork`),
Scrypted, go2rtc. Le VLAN 1 a de toute façon un accès complet aux autres VLAN.

### Transferts complets

```mermaid
flowchart LR
    wan((Internet))
    vpnc(("VPN commercial"))
    lan["1 · lan"]
    home["3 · home"]
    vpn["wg0 · vpn"]
    mm["4 · multimedia"]
    sec["5 · security"]
    iot["6 · iot"]
    car["7 · car"]
    guest["2 · guest"]

    lan ==> home & mm & sec & iot & car & wan
    home ==> lan & mm & iot & wan
    vpn ==> lan & mm & iot & wan
    mm ==> wan
    sec ==> wan
    iot ==> wan
    car ==> wan
    guest ==> vpnc ==> wan

    classDef trusted fill:#cfe2ff,stroke:#3d6fb6,color:#000
    classDef limited fill:#d1e7dd,stroke:#2f7d55,color:#000
    classDef isolated fill:#f8d7da,stroke:#b02a37,color:#000
    class lan,home,vpn trusted
    class mm,iot,car limited
    class sec,guest isolated
```

Zones de confiance (bleu) : `lan`, `home`, `vpn`. Zones restreintes (vert) : Internet
plus quelques services du VLAN 1. Zones isolées (rouge) : Internet seulement.

### Règles ciblées vers le VLAN 1

```mermaid
flowchart LR
    mm["4 · multimedia"]
    iot["6 · iot"]
    car["7 · car"]

    subgraph lan["VLAN 1 · lan"]
        ma["Music Assistant<br/>.34 + nœuds .100–.105"]
        jf["Jellyfin .29"]
        mqtt["Mosquitto .21, .38"]
        ha["Home Assistant<br/>.32 + nœuds .100–.105"]
        nd["Navidrome .33"]
    end

    mm -- "TCP 8095, 8097" --> ma
    mm -- "TCP 8096" --> jf
    iot -- "TCP 1883, 8883" --> mqtt
    iot -- "TCP 80, 8123" --> ha
    car -- "TCP 4533" --> nd
```

| Règle (`plan.yaml`)                | Source     | Destination            | Ports TCP  | Usage                                   |
| ---------------------------------- | ---------- | ---------------------- | ---------- | --------------------------------------- |
| `multimedia-music-assistant-nodes` | multimedia | `1.100`–`.105`         | 8095, 8097 | Cast : flux audio de Music Assistant    |
| `multimedia-music-assistant`       | multimedia | `1.34`                 | 8095, 8097 | idem, IP MetalLB                        |
| `multimedia-jellyfin`              | multimedia | `1.29`                 | 8096       | Cast depuis Jellyfin                    |
| `iot-mqtt`                         | iot        | `1.21`, `1.38`         | 1883, 8883 | Mosquitto (ESPHome, objets domotiques…) |
| `iot-home-assistant`               | iot        | `1.32`, `1.100`–`.105` | 80, 8123   | Objets qui poussent vers Home Assistant |
| `car-navidrome`                    | car        | `1.33`                 | 4533       | Musique du véhicule                     |

### Matrice des flux

Lecture : ligne = source, colonne = destination. ✅ tout, 🔸 règles ciblées, ❌ refusé.

| Source ↓ / Dest. → | lan | home | guest | multimedia | security | iot | car | Internet           |
| ------------------ | --- | ---- | ----- | ---------- | -------- | --- | --- | ------------------ |
| **lan (1)**        | —   | ✅   | ❌    | ✅         | ✅       | ✅  | ✅  | ✅                 |
| **home (3)**       | ✅  | —    | ❌    | ✅         | ❌       | ✅  | ❌  | ✅                 |
| **vpn (wg0)**      | ✅  | ❌   | ❌    | ✅         | ❌       | ✅  | ❌  | ✅                 |
| **guest (2)**      | ❌  | ❌   | —     | ❌         | ❌       | ❌  | ❌  | via VPN commercial |
| **multimedia (4)** | 🔸  | ❌   | ❌    | —          | ❌       | ❌  | ❌  | ✅                 |
| **security (5)**   | ❌  | ❌   | ❌    | ❌         | —        | ❌  | ❌  | ✅                 |
| **iot (6)**        | 🔸  | ❌   | ❌    | ❌         | ❌       | —   | ❌  | ✅                 |
| **car (7)**        | 🔸  | ❌   | ❌    | ❌         | ❌       | ❌  | —   | ✅                 |

Conséquences :

- Le VLAN 5 n'est joignable que depuis le VLAN 1 (Frigate sur le Mac mini, go2rtc) : ni
  `home` ni le VPN n'atteignent le NVR ; son accès distant passe par le cloud du fabricant.
- `home` pilote les objets (`iot`, `multimedia`) directement ; l'inverse est refusé.
- Le VPN WireGuard a les droits de `home`, sauf vers `home` lui-même. Il garde ses alias
  (DNAT `10.50.1.x` vers le NAS, le DNS du routeur et Traefik).

### Accès au routeur lui-même (`input`)

| Zone            | Politique | Autorisé                                        |
| --------------- | --------- | ----------------------------------------------- |
| lan, home, vpn  | accepter  | tout, dont l'administration (SSH, LuCI)         |
| guest           | refuser   | DHCP (UDP 67), DNS (53)                         |
| multimedia, iot | refuser   | DHCP, DNS, ICMP echo, mDNS (UDP 5353 multicast) |
| security, car   | refuser   | DHCP, DNS, ICMP echo                            |
| wan             | refuser   | règles OpenWRT par défaut (DHCP, ICMP, IGMP…)   |

### Entrées depuis Internet

| Redirection | Protocole / port | Destination           |
| ----------- | ---------------- | --------------------- |
| WireGuard   | UDP 51820        | routeur (`wg0`)       |
| Soulseek    | TCP 50300        | slskd, `192.168.1.36` |

Les redirections Traefik 80/443 sont désactivées : l'exposition web passe par le tunnel
Cloudflare (sortant).

## DNS

```mermaid
flowchart LR
    home["VLAN 3 · home"] -- "1er DNS" --> adg["AdGuard · 1.23<br/>(filtrage)"]
    home -. "2e DNS" .-> r3["routeur 3.1"]
    adg --> q9(("Quad9 DoH"))

    others["VLAN 1, 4, 5, 6, 7"] --> rx["routeur 192.168.x.1<br/>dnsmasq, sans filtrage"]
    r3 --> isp(("DNS du FAI"))
    rx --> isp

    guest["VLAN 2 · guest"] --> g8(("8.8.8.8<br/>via VPN commercial"))
```

- Seul le VLAN 3 (`adguard: true` dans `plan.yaml`) reçoit AdGuard : les objets ne cassent
  pas sur un domaine bloqué (cas d'un objet cloud qui ne téléchargeait plus son contenu), et
  les VLAN 4 à 7 n'ont aucun accès à AdGuard.
- dnsmasq du routeur sert aussi les noms locaux en `.lan` (baux et réservations).
- Le multimedia utilise parfois des DNS Google codés en dur : autorisés (Internet ouvert).

## mDNS

Avahi (`avahi-nodbus-daemon`) relaie le mDNS entre les VLAN 1, 3, 4 et 6 : Cast, AirPlay,
HomeKit, imprimante. Le relais rend la **découverte** visible entre ces quatre VLAN ; le
**trafic** reste soumis au pare-feu ci-dessus. Pas de relais pour les VLAN 2, 5 et 7.

```mermaid
flowchart LR
    avahi{{"Avahi · routeur"}}
    avahi <--> l1["1 · lan<br/>Home Assistant, Music Assistant"]
    avahi <--> l3["3 · home<br/>iPhone, Mac, Apple TV"]
    avahi <--> l4["4 · multimedia<br/>Cast"]
    avahi <--> l6["6 · iot<br/>ESPHome, imprimante"]
```

## Points ouverts

- Ports d'accès du switch (cluster, NAS, Mac mini) encore en trunk : un appareil
  compromis peut y envoyer des trames taguées dans n'importe quel VLAN sans passer par
  le pare-feu (durcissement en fin de migration, `switch-checklist.md`).
- Groupe `car` non migré (AirPort Express sur le port 3).
- Internet du VLAN 6 encore ouvert (« à restreindre » dans le plan).
