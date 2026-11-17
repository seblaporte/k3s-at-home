# Intégrations à mettre à jour

Relevé du 2026-11-16 (Home Assistant `core.config_entries`, dépôt). Seules les
intégrations qui visent une **adresse IP** sont concernées. À traiter **au moment où
l'appareil change de VLAN** (étape 4.6), pas avant : l'ancienne adresse reste valable
jusque-là.

« Auto » : Home Assistant met l'adresse à jour seul grâce à la découverte (zeroconf ou
DHCP), si le mDNS est relayé (`vlan.sh apply mdns`) et le VLAN joignable depuis le
VLAN 1. Vérifier quand même que l'entité revient en ligne.

## Home Assistant

| Groupe     | Intégration        | Appareil                      | IP actuelle     | IP cible     | Mise à jour                     |
| ---------- | ------------------ | ----------------------------- | --------------- | ------------ | ------------------------------- |
| multimedia | `androidtv_remote` | Chromecast Google TV          | `1.154`         | `4.15`       | auto (zeroconf), sinon recréer  |
| multimedia | `cast`             | Nest, Home Mini, Chromecast   | découverte      | VLAN 4       | auto (mDNS)                     |
| multimedia | `music_assistant`  | Music Assistant               | `1.34`, `1.105` | inchangée    | aucune ; doublon `1.105` à voir |
| home       | `apple_tv`         | Apple TV                      | découverte      | `3.10`       | auto (mDNS)                     |
| security   | `hikvision`        | NVR                           | `1.3`           | `5.10`       | reconfigurer l'hôte             |
| security   | `hikvision_next`   | NVR                           | `1.3`           | `5.10`       | reconfigurer l'URL              |
| iot        | `esphome` (×9)     | appareils ESPHome             | `1.40`–`.50`    | `6.40`–`.50` | auto (zeroconf)                 |
| iot        | `shelly`           | Shelly Pro 1                  | `1.45`          | `6.45`       | auto (zeroconf), sinon recréer  |
| iot        | `tuya_local`       | objet Tuya local              | `1.180`         | `6.68`       | reconfigurer l'hôte             |
| iot        | `dreame_vacuum`    | aspirateur                    | `1.239`         | `6.61`–`.63` | reconfigurer l'hôte             |
| iot        | `octoprint`        | OctoPrint                     | `1.214`         | `6.64`       | reconfigurer l'hôte             |
| iot        | `ipp`              | imprimante Epson              | `1.193`         | `6.65`       | auto (zeroconf), sinon recréer  |
| nettoyage  | `ipp`              | ancienne imprimante (absente) | `1.254`         | —            | supprimer l'intégration         |

Sans adresse IP (cloud ou découverte) : `roborock`, `google_assistant`, `homekit`,
`homekit_controller` (Scrypted, dans le VLAN 1).

## Hors Home Assistant

| Groupe   | Où                                   | Quoi                                            | Mise à jour                                  |
| -------- | ------------------------------------ | ----------------------------------------------- | -------------------------------------------- |
| security | `cluster/base/cluster-settings.yaml` | `NVR_ADDR`, utilisé par les flux RTSP de go2rtc | `192.168.1.3` → `192.168.5.10`, commit, Flux |
| security | Frigate (Mac mini, hors dépôt)       | flux RTSP du NVR                                | `192.168.1.3` → `192.168.5.10`, redémarrer   |
| security | Scrypted (interface web)             | caméras / NVR                                   | adresse du NVR → `192.168.5.10`              |
| iot      | ESPHome (`task esphome:*`)           | `IP=` des commandes OTA et logs                 | `192.168.6.4x` une fois déplacés             |
| car      | lecteur du véhicule                  | serveur Navidrome                               | inchangé (`1.33`), règle de pare-feu prévue  |

## ESPHome : ordre à respecter

`manual_ip` est retiré des fichiers de `cluster/apps/esphome/devices/` : au prochain
flash, chaque appareil demande son adresse au DHCP.

1. `vlan.sh apply vlans` avec les réservations `ip_lan` (`192.168.1.4x`) et `ip`
   (`192.168.6.4x`) de chaque ESPHome dans `devices.sops.yaml`.
2. Flash OTA de chaque appareil **pendant qu'il est encore dans le VLAN 1** :
   `task esphome:run DEVICE=<nom> IP=192.168.1.4x`. Il revient à la même adresse, par
   le DHCP cette fois.
3. À l'étape 4.6 (groupe iot) : `radius: true` dans `devices.sops.yaml`,
   `vlan.sh apply radius`, puis redémarrage de l'appareil : il prend `192.168.6.4x`.
