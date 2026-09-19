# Cookbook — Migrer une app vers le Tunnel Cloudflare (domaine plat)

Ce document permet de reproduire, app par app, la migration validée sur `slskd` le 19 septembre 2026 :
remplacer l'exposition via port-forward OpenWRT (`*.${SECRET_DOMAIN}`) par le Cloudflare Tunnel,
sur un hostname "plat" à un seul niveau (`<app>.${SECRET_APEX_DOMAIN}`), sans casser l'ancien hostname ni les
autres apps. Conçu pour être suivi tel quel dans une session Claude Code indépendante, sans contexte préalable.

**⚠️ Ce fichier est dans un repo public — ne jamais y écrire le domaine réel, une IP, ou un secret en clair.**
Partout ci-dessous, `<APEX_DOMAIN>` est un placeholder : à remplacer mentalement par la valeur réelle au moment
d'exécuter une commande, jamais en modifiant ce fichier. Récupérer cette valeur uniquement via
`sops -d cluster/base/cluster-secrets.sops.yaml | grep SECRET_APEX_DOMAIN` (cette seule ligne, jamais tout le
fichier).

**Principe directeur : chaque étape est additive.** On n'enlève et on ne remplace jamais rien tant que la
migration complète (toutes les apps + retrait du port-forward) n'est pas décidée. Si une étape échoue, l'app
continue de fonctionner sur son ancien hostname.

## État de départ (déjà en place, ne pas refaire)

Vérifier que ces éléments existent avant de commencer — s'ils manquent, ce cookbook ne s'applique pas tel quel :

- [ ] Un tunnel Cloudflare `k3s-homelab` existe déjà (`cloudflared tunnel list` doit le montrer).
- [ ] `cluster/apps/networking/cloudflared/` existe et est déployé (`kubectl -n networking get deployment cloudflared`).
- [ ] `cluster-secrets.sops.yaml` contient déjà `SECRET_APEX_DOMAIN` (le domaine racine, sans le sous-domaine
      qu'utilise `SECRET_DOMAIN`). Vérifier : `sops -d cluster/base/cluster-secrets.sops.yaml | grep SECRET_APEX_DOMAIN`
      (uniquement cette ligne, jamais tout le fichier).
- [ ] Traefik a `sniStrict: false` dans `cluster/apps/networking/traefik/helm-release.yaml` (bloc `tlsOptions.default`).
- [ ] Traefik a `forwardedHeaders.trustedIPs=10.42.0.0/16` sur les entrypoints `web` et `websecure`
      (`additionalArguments` du HelmRelease Traefik) — vérifier que le CIDR correspond toujours au pod CIDR du
      cluster (`kubectl get nodes -o jsonpath='{.items[0].spec.podCIDR}'`, doit être dans `10.42.0.0/16`).
- [ ] Le DNS wildcard `*.home.${SECRET_APEX_DOMAIN}` pointe toujours en direct sur l'IP WAN (DNS only, pas
      proxied) — c'est le filet de sécurité pour toutes les apps pas encore migrées. Ne jamais y toucher dans ce
      cookbook.

Si l'un de ces points manque, s'arrêter et se référer au plan complet de mise en place initiale plutôt qu'à ce
cookbook (celui-ci ne couvre que la migration app-par-app, pas la mise en place du tunnel lui-même).

## Recette, étape par étape

Remplacer `<APP>` par le nom de l'app (ex. `jellyfin`), `<NS>` par son namespace, `<HOST_ANCIEN>` par son
hostname actuel complet (ex. `jellyfin.${SECRET_DOMAIN}`, valeur réelle jamais écrite dans ce fichier).

### 1. Ajouter le hostname plat à l'ingress de l'app (Git)

Fichier : `cluster/apps/<NS>/<APP>/helm-release.yaml`. Dans le bloc `ingress.main`, dupliquer l'entrée
existante en ajoutant un second host et un second bloc `tls` — **ne pas toucher à l'entrée existante** :

```yaml
hosts:
  - host: "<APP>.${SECRET_DOMAIN}"          # existant, ne pas toucher
    paths: [...]                            # existant, ne pas toucher
  - host: "<APP>.${SECRET_APEX_DOMAIN}"     # nouveau
    paths:
      - path: /
        pathType: Prefix
        service:
          name: main                        # doit matcher le nom du service existant
tls:
  - hosts:
      - "<APP>.${SECRET_DOMAIN}"
    secretName: <APP>-tls-secret            # existant, ne pas toucher
  - hosts:
      - "<APP>.${SECRET_APEX_DOMAIN}"
    secretName: <APP>-flat-tls-secret       # nouveau, nom DIFFÉRENT — jamais réutiliser le secretName existant
```

**Pourquoi un `secretName` séparé** : si le nouveau certificat a un problème d'émission, ça ne doit jamais
pouvoir affecter le certificat existant qui fonctionne déjà pour les vrais utilisateurs.

### 2. Ajouter la règle d'ingress dans la config du tunnel (Git)

Fichier : `cluster/apps/networking/cloudflared/configmap.yaml`. Ajouter une entrée **avant** la règle wildcard
`*.${SECRET_DOMAIN}` et avant le catch-all `http_status:404` final (l'ordre compte — la première règle qui
matche gagne) :

```yaml
ingress:
  - hostname: "<APP>.${SECRET_APEX_DOMAIN}"
    originRequest:
      noTLSVerify: true     # temporaire — voir "Pièges connus" (bootstrap du tout premier certificat)
    service: https://traefik.networking.svc.cluster.local:443
  # ... règles existantes des apps déjà migrées, puis le wildcard, puis le catch-all
```

**`noTLSVerify: true` ici est volontaire et temporaire** (voir "Pièges connus" ci-dessous) — le
premier certificat ne peut pas s'émettre avec `matchSNItoHost: true` tant qu'aucun certificat valide
n'existe encore pour ce hostname. On rebascule sur `matchSNItoHost: true` à l'étape 5bis, une fois le
certificat `READY`.

### 3. Committer, pousser, reconcilier

```bash
git add cluster/apps/<NS>/<APP>/helm-release.yaml cluster/apps/networking/cloudflared/configmap.yaml
git commit -m "feat(<APP>): add flat apex-domain hostname as tunnel target"
git push
flux reconcile kustomization apps --with-source
kubectl -n networking rollout restart deployment cloudflared
kubectl -n networking rollout status deployment cloudflared --timeout=60s
```

### 4. Router le DNS vers le tunnel

```bash
cloudflared tunnel route dns k3s-homelab <APP>.<APEX_DOMAIN>
```

Vérifier que ça crée bien un CNAME **Proxied** (pas la peine d'aller vérifier sur le dashboard, la commande le
confirme dans sa sortie).

### 5. Attendre l'émission du certificat

```bash
kubectl -n <NS> get certificate <APP>-flat-tls-secret -w
```

Attendre `READY: True`. Si ça reste bloqué en `pending` plus de 2-3 minutes, voir "Pièges connus" ci-dessous
(HTTP-01 via le tunnel, `sniStrict`).

### 5bis. Basculer vers `matchSNItoHost` (Git)

Une fois `READY: True` confirmé, repasser la règle du tunnel en vérification TLS stricte — ne jamais
laisser `noTLSVerify: true` en état final :

```yaml
  - hostname: "<APP>.${SECRET_APEX_DOMAIN}"
    originRequest:
      matchSNItoHost: true
    service: https://traefik.networking.svc.cluster.local:443
```

```bash
git add cluster/apps/networking/cloudflared/configmap.yaml
git commit -m "test(<APP>): enable strict TLS verification on the flat hostname"
git push
flux reconcile kustomization apps --with-source
kubectl -n networking rollout restart deployment cloudflared
kubectl -n networking rollout status deployment cloudflared --timeout=60s
```

### 6. Ajouter la règle Authelia (⚠️ hors Git — voir avertissement)

**Ceci modifie le pod Authelia en direct, pas via Git.** Cette limitation est connue et documentée dans
"Pièges connus" — ne pas essayer de la contourner sans relire cette section.

D'abord, vérifier la ligne à dupliquer sans jamais afficher tout le fichier (qui contient des secrets) :

```bash
kubectl -n networking exec deploy/authelia -- sh -c "grep -in 'policy\|access_control\|^- domain\|  - domain' /config/configuration.yml"
```

Repérer le numéro de ligne de la règle `*.${SECRET_DOMAIN}` (policy `two_factor`) et de sa ligne
`policy:` juste après (ex. lignes 42-43). Insérer juste après, avec le numéro de ligne réel constaté et la
vraie valeur de `<APEX_DOMAIN>` (jamais écrite dans ce fichier — voir avertissement en haut de page) :

```bash
POD=$(kubectl -n networking get pod -l app.kubernetes.io/name=authelia -o jsonpath='{.items[0].metadata.name}')
kubectl -n networking exec -i "$POD" -- sed -i -f /dev/stdin /config/configuration.yml <<'SEDSCRIPT'
<NUMERO_LIGNE>a\
    - domain: '<APP>.<APEX_DOMAIN>'\
      policy: 'two_factor'
SEDSCRIPT
kubectl -n networking rollout restart deployment authelia
kubectl -n networking rollout status deployment authelia --timeout=90s
```

Revérifier avec le même `grep` non-sensible qu'au-dessus que la ligne a bien été ajoutée, sans dupliquer une
règle existante.

### 7. Tester de bout en bout

La résolution DNS locale (via le routeur OpenWRT) peut mettre du temps à se mettre à jour ou garder un cache
négatif périmé — toujours tester en pointant directement vers l'IP Cloudflare pour éviter les faux négatifs :

```bash
IP=$(dig +short A <APP>.<APEX_DOMAIN> @1.1.1.1 | head -1)
curl --resolve <APP>.<APEX_DOMAIN>:443:$IP -sI https://<APP>.<APEX_DOMAIN>/
```

Attendu : `HTTP/2 302` avec un `location:` pointant vers le portail Authelia (`auth.${SECRET_DOMAIN}/?rd=...`).
Un `403` direct = la règle Authelia (étape 6) manque ou n'a pas été bien insérée. Un `502` = voir "Pièges connus".

Vérifier aussi que la vraie IP cliente est visible côté Traefik (pas l'IP d'un pod `10.42.x.x`) :

```bash
kubectl -n networking logs -l app.kubernetes.io/name=traefik --since=1m --prefix | grep "<APP>-<NS>"
```

## Pièges connus (déjà rencontrés, ne pas redécouvrir)

- **Le certificat Cloudflare gratuit ne couvre qu'un seul niveau de sous-domaine.** C'est pourquoi on utilise
  `${SECRET_APEX_DOMAIN}` (plat) et pas `${SECRET_DOMAIN}` (qui contient déjà `home.`) pour le nouveau
  hostname. Utiliser le mauvais domaine donne un échec TLS **à la frontière Cloudflare**, avant même d'atteindre
  le tunnel (symptôme : `curl` renvoie `TLS handshake failure` en `-v`, pas un code HTTP).
- **`sniStrict` bloque uniquement la toute première émission d'un certificat**, pas les renouvellements. Déjà
  corrigé globalement (`sniStrict: false`) — ne pas le re-désactiver par erreur en pensant que c'est nécessaire
  à nouveau, il l'est déjà.
- **`matchSNItoHost: true`, jamais `noTLSVerify: true`, en état final** sur les nouvelles règles. `noTLSVerify`
  fonctionne mais désactive toute vérification du certificat d'origine. `matchSNItoHost` donne une vraie
  vérification TLS de bout en bout — c'est le standard pour toute app migrée, une fois son certificat émis.
- **Le tout premier certificat d'un hostname plat ne peut PAS s'émettre avec `matchSNItoHost: true` dès le
  départ — c'est un problème d'œuf-et-poule.** Le challenge HTTP-01 passe par le tunnel jusqu'à Traefik en
  HTTPS ; tant qu'aucun certificat n'existe pour ce hostname, Traefik répond avec son certificat interne
  auto-généré (`*.traefik.default`), que `matchSNItoHost` rejette aussitôt (`x509: certificate is valid for
  ...traefik.default, not <app>.<apex>`) — la commande cert-manager reste bloquée en `pending` indéfiniment.
  Symptôme côté challenge : `Waiting for HTTP-01 challenge propagation: wrong status code '502'`. Solution :
  démarrer avec `noTLSVerify: true` (étape 2), attendre `READY: True` (étape 5), puis rebasculer sur
  `matchSNItoHost: true` (étape 5bis). C'est exactement ce qui a été fait pour `slskd` (voir son historique
  git sur `cluster/apps/networking/cloudflared/configmap.yaml`) — cette étape de bootstrap avait été omise
  dans une version précédente de ce cookbook.
- **Le fichier de config Authelia n'est pas dans Git** (il vit sur un volume persistant). Toute règle ajoutée
  par ce cookbook doit être ré-appliquée si le pod Authelia est recréé sur un nouveau volume (rare, mais possible
  après une panne ou une migration de PV). Envisager, une fois plusieurs apps migrées, de faire un chantier
  séparé pour verser ce fichier dans Git via un Secret + ConfigMap — hors scope de ce cookbook.
- **Ne jamais faire un `cat` ou un `grep -A5` large sur `/config/configuration.yml` d'Authelia** — il contient
  des secrets (clé de session, de chiffrement du stockage). Toujours utiliser un grep ciblé sur des mots-clés
  non sensibles (`domain`, `policy`, `access_control`) sans contexte étendu.
- **DNS négatif en cache côté routeur OpenWRT** : après une modif DNS récente, un test local peut échouer alors
  que ça fonctionne réellement — toujours vérifier via `dig ... @1.1.1.1` (bypass le résolveur local) avant de
  conclure à un problème.
- **Ne jamais réutiliser un `secretName` de certificat existant** pour le nouveau hostname (voir étape 1) — un
  échec d'émission sur le nouveau ne doit jamais pouvoir affecter l'ancien.

## Garde-fous — toujours demander confirmation explicite avant de :

- Modifier `cluster-secrets.sops.yaml` ou tout fichier sous `cluster/base/flux-system/`.
- Écrire en clair un domaine ou un secret dans un fichier versionné (toujours passer par une variable
  `${SECRET_...}` existante).
- Modifier le pod Authelia en direct (étape 6) — nommer explicitement cette action précise au moment de la
  demander, une confirmation vague ne suffit pas.
- Toucher au wildcard `*.${SECRET_DOMAIN}` ou au port-forward OpenWRT — ça reste le filet de sécurité de toutes
  les apps pas encore migrées, jusqu'à la décision finale de cutover complet.

## Quand toutes les apps sont migrées

Ce cookbook s'arrête à "une app fonctionne sur son hostname plat, en parallèle de l'ancien". La suite
(basculer réellement les utilisateurs vers les nouveaux hostnames, puis retirer le port-forward OpenWRT) est
une décision séparée, à ne prendre qu'une fois **toutes** les apps migrées et validées individuellement —
voir le plan de mise en place initial du Tunnel pour cette étape finale (bascule DNS progressive avec TTL
abaissé, puis désactivation du port-forward).
