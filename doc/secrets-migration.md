# Migration des secrets vers Bitwarden Secrets Manager

Table de correspondance entre les clés SOPS d'origine et les secrets Bitwarden (noms uniquement, aucune valeur).

**État : terminée (2026-11-19).**

1. ✅ **Copie** : des `PushSecret` ont envoyé les Secrets du cluster vers Bitwarden (PR #1080), puis ont été supprimés
   (`deletionPolicy: None` : les secrets restent dans Bitwarden).
2. ✅ **Bascule** : toutes les applications lisent des `ExternalSecret` (PR #1081 à #1087), valeurs vérifiées identiques
   (empreintes `configDigest` des HelmReleases, SHA-256 des Secrets et des variables des CronJobs).
3. ✅ **Nettoyage** : compte machine `eso-k3s` en lecture seule ; `cluster-secrets.sops.yaml` ne garde que les 10 clés
   « reste dans SOPS » ; les fichiers SOPS par application sont supprimés.

Les valeurs `restic-os-identity-api-version` et `restic-os-tenant-name` contenaient des guillemets littéraux (astuce de
substitution Flux) : les templates ESO les retirent (`trimAll`), que Bitwarden les contienne ou non.

| Source (namespace/Secret) | Clé | Secret Bitwarden | Statut |
|---|---|---|---|
| `flux-system/cluster-secrets` | `SECRET_DOMAIN` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_CLOUDFLARE_EMAIL` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_BITWARDEN_DATABASE_URL` | `vaultwarden-database-url` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_BITWARDEN_ADMIN_TOKEN` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_GRAFANA_ADMIN_USERNAME` | `grafana-admin-username` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_GRAFANA_ADMIN_PASSWORD` | `grafana-admin-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TESLAMATE_DB_PASSWORD` | `teslamate-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TESLAMATE_ENCRYPTION_KEY` | `teslamate-encryption-key` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_LINKY_INFLUX_DB_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_DEFAULT_USER_INFLUX_DB_PASSWORD` | `default-user-influx-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TYDOM_MAC` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_TYDOM_PASSWORD` | `tydom-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TYDOM_IP` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_TYDOM_ALARM_PIN` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_HIKVISION_USERNAME` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_HIKVISION_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_CROWDSEC_ENROLL_KEY` | `crowdsec-enroll-key` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_UNIFI_DB_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_AUTH_URL` | `restic-os-auth-url` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_IDENTITY_API_VERSION` | `restic-os-identity-api-version` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_TENANT_ID` | `restic-os-tenant-id` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_TENANT_NAME` | `restic-os-tenant-name` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_USERNAME` | `restic-os-username` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_PASSWORD` | `restic-os-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_OS_REGION_NAME` | `restic-os-region-name` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_PASSWORD` | `restic-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RESTIC_REPOSITORY` | `restic-repository` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_HOME_ASSISTANT_DB_PASSWORD` | `home-assistant-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_BITWARDEN_DB_PASSWORD` | `vaultwarden-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_MEMOS_DB_PASSWORD` | `memos-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TRANSMISSION_USER` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_TRANSMISSION_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_JOAL_TOKEN` | `joal-token` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_VPN_USERNAME` | `vpn-username` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_VPN_PASSWORD` | `vpn-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_UNPOLLER_PASSWORD` | `unpoller-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_SONARR_DB_PASSWORD` | `sonarr-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_RADARR_DB_PASSWORD` | `radarr-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_PROWLARR_DB_PASSWORD` | `prowlarr-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_K3S_DB_PASSWORD` | `k3s-db-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_TANDOOR_DB_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_TANDOOR_KEY` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_NAVIDROME_LASTFM_APIKEY` | `navidrome-lastfm-apikey` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_NAVIDROME_LASTFM_SECRET` | `navidrome-lastfm-secret` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_NAVIDROME_SPOTIFY_ID` | `navidrome-spotify-id` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_NAVIDROME_SPOTIFY_SECRET` | `navidrome-spotify-secret` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_LINKWARDEN_DB_PASSWORD` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_LINKWARDEN_NEXTAUTH_SECRET` | — | à supprimer (inutilisée) |
| `flux-system/cluster-secrets` | `SECRET_UPTIME_KUMA_ADMIN_EMAIL` | `uptime-kuma-admin-email` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_UPTIME_KUMA_ADMIN_PASSWORD` | `uptime-kuma-admin-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_MINIO_ROOT_USER` | `minio-root-user` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_MINIO_ROOT_PASSWORD` | `minio-root-password` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_LOKI_S3_ACCESS_KEY` | `loki-s3-access-key` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_LOKI_S3_SECRET_KEY` | `loki-s3-secret-key` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_GHCR_PULL_TOKEN` | `ghcr-pull-token` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_GHCR_USERNAME` | `ghcr-username` | → Bitwarden |
| `flux-system/cluster-secrets` | `GO2RTC_BASIC_AUTH_HEADER` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_APEX_DOMAIN` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_HOME_IPV6_PREFIX` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_HOME_IPV4` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_ALERTMANAGER_TELEGRAM_BOT_TOKEN` | `alertmanager-telegram-bot-token` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_ALERTMANAGER_TELEGRAM_CHAT_ID` | `alertmanager-telegram-chat-id` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_WATCHDOG_HEALTH_CHECK_UUID` | `watchdog-health-check-uuid` | → Bitwarden |
| `flux-system/cluster-secrets` | `SECRET_BITWARDEN_ORGANIZATION_ID` | — | reste dans SOPS |
| `flux-system/cluster-secrets` | `SECRET_BITWARDEN_PROJECT_ID` | — | reste dans SOPS |
| `esphome/esphome-secrets` | `secrets.yaml` | `esphome-secrets-yaml` | → Bitwarden |
| `go2rtc/go2rtc-secrets` | `NVR_USERNAME` | `go2rtc-nvr-username` | → Bitwarden |
| `go2rtc/go2rtc-secrets` | `NVR_PASSWORD` | `go2rtc-nvr-password` | → Bitwarden |
| `go2rtc/go2rtc-secrets` | `GO2RTC_USERNAME` | `go2rtc-username` | → Bitwarden |
| `go2rtc/go2rtc-secrets` | `GO2RTC_PASSWORD` | `go2rtc-password` | → Bitwarden |
| `kitchenvault-dev/kitchenvault-secrets` | `postgres-user-password` | `kitchenvault-dev-postgres-user-password` | → Bitwarden |
| `kitchenvault-dev/kitchenvault-secrets` | `ovh-ai-endpoints-access-token` | `kitchenvault-dev-ovh-ai-endpoints-access-token` | → Bitwarden |
| `kitchenvault-dev/kitchenvault-secrets` | `COOKIDOO_EMAIL` | `kitchenvault-dev-cookidoo-email` | → Bitwarden |
| `kitchenvault-dev/kitchenvault-secrets` | `COOKIDOO_PASSWORD` | `kitchenvault-dev-cookidoo-password` | → Bitwarden |
| `kitchenvault/kitchenvault-secrets` | `postgres-user-password` | `kitchenvault-production-postgres-user-password` | → Bitwarden |
| `kitchenvault/kitchenvault-secrets` | `ovh-ai-endpoints-access-token` | `kitchenvault-production-ovh-ai-endpoints-access-token` | → Bitwarden |
| `kitchenvault/kitchenvault-secrets` | `COOKIDOO_EMAIL` | `kitchenvault-production-cookidoo-email` | → Bitwarden |
| `kitchenvault/kitchenvault-secrets` | `COOKIDOO_PASSWORD` | `kitchenvault-production-cookidoo-password` | → Bitwarden |
| `mosquitto/mosquitto-secure-secrets` | `MQTT_USERNAME` | `mosquitto-mqtt-username` | → Bitwarden |
| `mosquitto/mosquitto-secure-secrets` | `MQTT_PASSWORD` | `mosquitto-mqtt-password` | → Bitwarden |
| `networking/authelia-secrets` | `JWT_SECRET` | `authelia-jwt-secret` | → Bitwarden |
| `networking/authelia-secrets` | `SESSION_SECRET` | `authelia-session-secret` | → Bitwarden |
| `networking/authelia-secrets` | `STORAGE_ENCRYPTION_KEY` | `authelia-storage-encryption-key` | → Bitwarden |
| `networking/authelia-secrets` | `users_database.yml` | `authelia-users-database-yml` | → Bitwarden |
| `networking/cloudflared-credentials` | `credentials.json` | `cloudflared-credentials-json` | → Bitwarden |
| `cert-manager/cloudflare-token-secret` | `cloudflare-token` | `cert-manager-cloudflare-token` | → Bitwarden |
| `kured/kured-secret` | `notifyUrl` | `kured-notify-url` | → Bitwarden |

Total : 65 à migrer, 10 restent dans SOPS, 12 à supprimer.
