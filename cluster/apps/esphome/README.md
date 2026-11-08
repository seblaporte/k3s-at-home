# esphome-config

Configurations ESPHome de la maison, source de vérité pour le tableau de bord
ESPHome du cluster k3s (déployé par Flux depuis `seblaporte/k3s-at-home`).

- `devices/` : une configuration par appareil (importées du serveur le 2026-11-08).
- `secret.sops.yaml` : `secrets.yaml` chiffré avec SOPS (mêmes clés PGP que `k3s-at-home`).

Les clés API et mots de passe OTA, auparavant en clair dans chaque fichier, sont
désormais référencés par `!secret <appareil>_api_key` / `<appareil>_ota_password`.

## Même environnement sur le Mac et sur k3s

Le `Makefile` lance l'image `esphome/esphome` **du même tag que le cluster** (lu dans
`cluster/apps/esphome/helm-release.yaml` de `k3s-at-home`, mis à jour par Renovate),
avec la même organisation que le pod :

| Chemin | Contenu | Sur k3s |
|---|---|---|
| `/config` (lecture seule) | `devices/*.yaml`, `.gitignore`, `secrets.yaml` | ConfigMap `esphome-devices` + Secret `esphome-secrets` |
| `/components/<nom>` (lecture seule) | composants locaux | ConfigMap `esphome-component-<nom>` |
| `/data` | `ESPHOME_DATA_DIR` : builds, état | PVC `esphome-config` |
| `/cache` | toolchains PlatformIO | PVC `esphome-config` |

```bash
make help
make validate                      # tous les appareils ; DUMMY=1 sans clé PGP
make compile DEVICE=veilleuse-etoile
make run DEVICE=veilleuse-etoile IP=192.168.1.43   # compile + OTA
make logs DEVICE=veilleuse-etoile IP=192.168.1.43
make flash-usb DEVICE=… PORT=/dev/cu.usbserial-…  # 1er flash (esptool hors Docker)
make dashboard                     # http://localhost:6052, lecture seule comme sur k3s
```

`make secrets` déchiffre `secret.sops.yaml` en `secrets.yaml` (ignoré par Git) avec ta clé PGP.

## Ajouter ou retirer un appareil ou un composant

Créer/supprimer `devices/<nom>.yaml` (ou `components/<nom>/`), puis `make kustomization` :
kustomize ne sait pas lister un dossier, `kustomization.yaml` est généré. La CI vérifie
qu'il est à jour.

Les composants locaux se référencent par chemin absolu :

```yaml
external_components:
  - source:
      type: local
      path: /components
```

## CI

À chaque PR et push sur `main` : rendu kustomize, validation de tous les appareils
(secrets factices), compilation des appareils modifiés (tous si `components/`,
`Makefile`, `scripts/` ou la CI changent ; tous à la demande via « Run workflow »).

## Déploiement

Flux (`k3s-at-home`) lit ce dépôt, déchiffre `secret.sops.yaml` et applique les objets
dans le namespace `esphome`. Le kubelet met à jour `/config` sans redémarrer le pod.
**Flux ne flashe jamais un appareil** : l'installation reste une action manuelle
(bouton « Install » du tableau de bord ou `make run`).
