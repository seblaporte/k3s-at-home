# Datastore k3s (PostgreSQL sur le NAS)

Le serveur k3s n'utilise ni etcd embarqué ni SQLite : son datastore est un PostgreSQL
externe, accédé via kine.

| Élément            | Valeur                                                         |
| ------------------ | -------------------------------------------------------------- |
| Hôte               | NAS `192.168.1.10`, conteneur Docker, port `5454`              |
| Base / utilisateur | `k3s` / `k3s` (superutilisateur)                               |
| Version            | PostgreSQL 14.2                                                |
| Configuré dans     | `--datastore-endpoint` de l'unité systemd k3s sur `k3s-master` |

> Ce conteneur vit hors de ce repo : **Renovate ne le suit pas**. PostgreSQL 14.2
> (février 2022) a plusieurs années de correctifs de retard — à mettre à jour à la main.

## Pourquoi c'est important

Chaque écriture Kubernetes (leases, statuts, événements) est un `COMMIT` qui attend que
le disque du NAS confirme l'écriture (`fsync`). Le NAS héberge aussi les disques système
des nœuds (iSCSI) et les volumes iSCSI et NFS : sa latence d'écriture est partagée par
tout le cluster.

Quand un commit dépasse ~5 s, le kube-controller-manager ne peut plus renouveler son
bail de leader, k3s s'arrête et systemd le relance — et chaque redémarrage provoque une
nouvelle rafale d'écritures. Cette boucle a mis le cluster en panne le 2026-10-02 pendant
une mise à jour de k3s (762 « Slow SQL » en une heure, 3 à 5 s par `INSERT`).

## Réglage appliqué (2026-10-03)

L'essentiel du journal WAL (~5 Go/jour) était constitué de pages complètes : après chaque
checkpoint, la première modification d'une page la recopie en entier (8 Ko) dans le
journal, et k3s réécrit sans arrêt les mêmes lignes (ses leases). Espacer les checkpoints
et compresser ces pages réduit le volume écrit sans toucher à la durabilité (`fsync` et
`synchronous_commit` restent actifs).

| Paramètre            | Défaut | Appliqué    |
| -------------------- | ------ | ----------- |
| `checkpoint_timeout` | `5min` | `15min`     |
| `max_wal_size`       | `1GB`  | `2GB`       |
| `wal_compression`    | `off`  | `on` (pglz) |

Contreparties : après un crash, la reprise rejoue jusqu'à 15 min de journal (~50 Mo au
rythme actuel), `pg_wal` peut monter jusqu'à ~2 Go pendant une rafale, et la compression
consomme un peu de CPU sur le NAS.

Les valeurs sont enregistrées par `ALTER SYSTEM` dans `postgresql.auto.conf`, dans le
répertoire de données : elles survivent aux redémarrages du conteneur tant que ce
répertoire est persistant.

### Vérifier

```sql
SELECT name, setting, unit, pending_restart
FROM pg_settings
WHERE name IN ('checkpoint_timeout', 'max_wal_size', 'wal_compression');
-- attendu : 900 s, 2048 MB, on, pending_restart = f
```

### Réappliquer (par exemple après reconstruction du conteneur)

Les trois paramètres se rechargent à chaud, sans redémarrage :

```sql
ALTER SYSTEM SET checkpoint_timeout = '15min';
ALTER SYSTEM SET max_wal_size = '2GB';
ALTER SYSTEM SET wal_compression = 'on';
SELECT pg_reload_conf();
```

### Revenir en arrière

```sql
ALTER SYSTEM RESET checkpoint_timeout;
ALTER SYSTEM RESET max_wal_size;
ALTER SYSTEM RESET wal_compression;
SELECT pg_reload_conf();
```

## Se connecter sans exposer les identifiants

`psql` n'est pas installé sur les nœuds. Depuis `k3s-master`, lancer un conteneur
éphémère et lire la chaîne de connexion dans l'unité systemd sans l'afficher :

```bash
EP=$(systemctl cat k3s | grep -oE 'postgres://[^" ]+')
sudo k3s ctr run --rm --net-host docker.io/library/postgres:16-alpine pg \
  psql "$EP" -X -c "SELECT now(), wal_bytes, wal_fpi FROM pg_stat_wal;"
```

Ne jamais coller la sortie de `systemctl cat k3s` : elle contient le token du cluster et
le mot de passe du datastore.

## Signaux de santé

- `journalctl -u k3s | grep -c "Slow SQL"` sur `k3s-master` : quelques-uns par jour
  (jusqu'à ~2 s) est la normale ; des centaines par heure signifient que le NAS sature.
- `leaderelection lost` suivi de `k3s.service: Main process exited` : le datastore est
  trop lent et k3s redémarre en boucle.
