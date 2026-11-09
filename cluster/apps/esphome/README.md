# ESPHome

Configurations ESPHome de la maison, source de vérité du tableau de bord ESPHome du cluster
(déployé par Flux avec le reste de `cluster/apps`).

- `devices/` : une configuration par appareil.
- `components/` : composants ESPHome locaux.
- `secret.sops.yaml` : le `secrets.yaml` d'ESPHome (Wi-Fi, clés API, mots de passe OTA, adresses
  MAC) chiffré avec SOPS, mêmes clés PGP que le reste du dépôt. Les configurations ne contiennent
  que des références `!secret <nom>`.
- `kustomization.yaml` : **généré** par `task esphome:kustomization` (kustomize ne sait pas lister
  un dossier). La CI vérifie qu'il est à jour.

## Même environnement sur le Mac et sur k3s

`task esphome:*` lance l'image `esphome/esphome` **du même tag que le cluster** (lu dans
`helm-release.yaml`, mis à jour par Renovate) avec la même organisation que le pod :

| Chemin                      | Contenu                                           | Sur k3s                                         |
| --------------------------- | ------------------------------------------------- | ----------------------------------------------- |
| `/config` (lecture seule)   | `devices/*.yaml`, `.gitignore`, `secrets.yaml`    | ConfigMap `esphome-devices` + Secret `esphome-secrets` |
| `/components/<nom>` (lecture seule) | composants locaux                         | ConfigMap `esphome-component-<nom>`             |
| `/data`                     | `ESPHOME_DATA_DIR` : builds, état                 | PVC `esphome-config`, dossier `data`            |
| `/cache`                    | toolchains PlatformIO                             | PVC `esphome-config`, dossier `cache`           |

```bash
task --list | grep esphome
task esphome:validate                  # tous les appareils ; DUMMY=1 sans clé PGP
task esphome:compile DEVICE=veilleuse-etoile
task esphome:run DEVICE=veilleuse-etoile IP=192.168.1.43   # compile + OTA
task esphome:logs DEVICE=veilleuse-etoile IP=192.168.1.43
task esphome:flash-usb DEVICE=… PORT=/dev/cu.usbserial-…   # 1er flash (esptool hors Docker)
task esphome:dashboard                 # http://localhost:6052, lecture seule comme sur k3s
```

## Secrets

- **Git** : `secret.sops.yaml` est un Secret Kubernetes dont `stringData."secrets.yaml"` est
  chiffré. Modifier : `sops cluster/apps/esphome/secret.sops.yaml`.
- **Mac** : `task esphome:secrets` le déchiffre en `secrets.yaml` (ignoré par Git, mode 600) avec ta
  clé PGP ; les autres tâches le font automatiquement.
- **Cluster** : Flux le déchiffre avec le secret `sops-gpg` et le projette dans `/config`.
- **CI** : aucune clé ; `scripts/dummy-secrets.py` génère des valeurs factices pour valider et
  compiler.

Ne pas confondre avec les `${SECRET_…}` de `cluster-secrets`, substitués par Flux dans les
manifestes : les fichiers ESPHome n'en utilisent pas. Inversement, leurs `${…}` (substitutions
ESPHome) ne doivent pas être substitués par Flux : la ConfigMap et le Secret portent l'annotation
`kustomize.toolkit.fluxcd.io/substitute: disabled`.

## Ajouter ou retirer un appareil ou un composant

Créer ou supprimer `devices/<nom>.yaml` (ou `components/<nom>/`), puis
`task esphome:kustomization`. Un nouveau composant doit aussi être monté dans `helm-release.yaml`
(`persistence`, sur le modèle de `component-inmotion-v1`) et référencé par chemin absolu :

```yaml
external_components:
  - source:
      type: local
      path: /components
```

Les secrets d'un nouvel appareil (`<appareil>_api_key`, `<appareil>_ota_password`) s'ajoutent avec
`sops cluster/apps/esphome/secret.sops.yaml`. Une clé API est un base64 de 32 octets
(`openssl rand -base64 32`).

## CI

`.github/workflows/esphome.yaml`, sur les PR et `main` qui touchent ce dossier : rendu kustomize,
validation de tous les appareils (secrets factices), compilation des appareils modifiés. Tout est
recompilé si `components/`, `scripts/`, le workflow ou la version d'ESPHome changent (donc sur les
PR Renovate) ; tout à la demande via « Run workflow ».

## Déploiement

Flux lit ce dossier, déchiffre `secret.sops.yaml` et applique les objets dans le namespace
`esphome`. Le volume projeté est mis à jour par le kubelet sans redémarrer le pod. Le tableau de
bord est en lecture seule : on modifie la configuration dans Git, jamais depuis l'interface.
**Flux ne flashe jamais un appareil** : l'installation reste une action manuelle (bouton
« Install » du tableau de bord ou `task esphome:run`).
