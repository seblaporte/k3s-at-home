# esphome-config

Configurations ESPHome de la maison, source de vérité pour le tableau de bord
ESPHome du cluster k3s (déployé par Flux depuis `seblaporte/k3s-at-home`).

- `devices/` : une configuration par appareil (importées du serveur le 2026-11-08).
- `secret.sops.yaml` : `secrets.yaml` chiffré avec SOPS (mêmes clés PGP que `k3s-at-home`).

Les clés API et mots de passe OTA, auparavant en clair dans chaque fichier, sont
désormais référencés par `!secret <appareil>_api_key` / `<appareil>_ota_password`.
