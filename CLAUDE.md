# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is a GitOps-driven Kubernetes homelab running on k3s (Raspberry Pi nodes), managed by Flux CD. All cluster state is declarative and lives in Git. Secrets are encrypted with SOPS/GnuPG.

## Required CLI Tools

`kubectl`, `flux`, `sops`, `gnupg`, `task`, `kustomize`, `helm`, `direnv`, `pre-commit`, `yamllint`, `prettier`

Run `direnv allow` once to load `KUBECONFIG` from `.envrc`.

## Common Commands

```bash
# Linting
task lint:all          # Run all linters (YAML, Markdown, formatting)
task lint:yaml         # YAML only
task lint:markdown     # Markdown only
task format:all        # Auto-format YAML and Markdown with Prettier

# Flux
task flux:sync         # Trigger Flux reconciliation

# Pre-commit
task pre-commit:run    # Run pre-commit hooks manually
task pre-commit:update # Update hook versions
```

## Repository Structure

```
cluster/
├── base/
│   ├── flux-system/       # Flux bootstrap (GitRepository, Kustomizations, HelmRepositories)
│   ├── cluster-settings.yaml  # ConfigMap: IP addresses, NAS addr, MetalLB pool
│   └── cluster-secrets.sops.yaml  # SOPS-encrypted: domain, credentials
├── crds/                  # CRDs (cert-manager, traefik) — no pruning
├── core/                  # Infrastructure (no pruning): metallb, cert-manager, monitoring, NFS
└── apps/                  # Applications (pruning enabled): 43+ namespaced app deployments
```

## Deployment Architecture

**Flux reconciliation order**: `flux-system` → `crds` → `core` → `apps`

Each layer depends on the previous. `core` and `crds` have pruning disabled (resources preserved even if removed from Git). `apps` has pruning enabled.

**Per-app structure** (typical):
```
cluster/apps/<namespace>/<appname>/
├── kustomization.yaml   # Composes namespace.yaml + helm-release.yaml [+ secret.sops.yaml]
├── namespace.yaml
├── helm-release.yaml    # HelmRelease pointing to a chart repo
└── secret.sops.yaml     # Optional SOPS-encrypted secret
```

## Key Patterns

**Variable substitution**: Kustomizations inject `cluster-settings` ConfigMap and `cluster-secrets` Secret values using `${VAR_NAME}` syntax. Service IPs are defined in `cluster/base/cluster-settings.yaml` and referenced as `${SVC_APPNAME_ADDR}` in HelmRelease values.

**LoadBalancer IPs**: All user-facing services use MetalLB with fixed IPs from pool `192.168.1.20–192.168.1.40`. Always add new services to `cluster-settings.yaml` with a unique IP in that range.

**Helm charts**: Primary sources are `k8s-at-home`, `bjw-s` (app-template), `bitnami`, `jetstack`, and `traefik` repositories — all defined as `HelmRepository` objects in `cluster/base/flux-system/charts/`. Exact `sourceRef.name` values: `bjw-s-helm-charts` (app-template **v2.2.0** — current version across all apps), `bitnami-charts`.

**SOPS encryption**: Only `data` and `stringData` fields are encrypted (per `.sops.yaml`). Files containing secrets must be named `*.sops.yaml`. Never commit unencrypted secrets.

**Ingress vs LoadBalancer**: Services exposed via Traefik Ingress only need a ClusterIP service — no MetalLB IP required. MetalLB IPs are only for services needing direct LAN access (MQTT, databases on NAS, etc.). Traefik itself is at `192.168.1.25` and handles all ingress traffic.

**Ingress annotations** (exact values to use):

```yaml
ingressClassName: "traefik"
annotations:
  traefik.ingress.kubernetes.io/router.entrypoints: "websecure"
  traefik.ingress.kubernetes.io/router.middlewares: networking-forwardauth-authelia@kubernetescrd
  cert-manager.io/cluster-issuer: "letsencrypt-production"  # or letsencrypt-staging for testing
```

**Storage classes**:

- `synology-iscsi-storage` — high-performance iSCSI on Synology NAS, use for databases
- `nfs-client` — dynamic NFS provisioning, use for general config/media storage

**Node affinity**: All app pods must exclude master nodes. Always include in `defaultPodOptions`:

```yaml
defaultPodOptions:
  affinity:
    nodeAffinity:
      requiredDuringSchedulingIgnoredDuringExecution:
        nodeSelectorTerms:
          - matchExpressions:
              - key: node-role.kubernetes.io/master
                operator: NotIn
                values:
                  - "true"
```

**Multi-service apps**: Create one HelmRelease file per service (e.g. `helm-release-backend.yaml`, `helm-release-frontend.yaml`, `helm-release-postgres.yaml`). Do not use multiple controllers in a single HelmRelease.

## Adding a New Application

1. Create `cluster/apps/<namespace>/<app>/` directory
2. Add `namespace.yaml`, `helm-release.yaml`, `kustomization.yaml`
3. Use the chart `app-template` from HelmRepository `bjw-s-helm-charts`
3. If the app needs a LoadBalancer IP, add `SVC_APPNAME_ADDR` to `cluster/base/cluster-settings.yaml`
4. If the app needs secrets, create `secret.sops.yaml` (encrypt with `sops -e -i`)
5. Add the app kustomization to `cluster/apps/kustomization.yaml`

## Secrets Management

```bash
# Encrypt a new secret file
sops -e -i cluster/apps/<ns>/<app>/secret.sops.yaml

# Edit an existing encrypted secret
sops cluster/apps/<ns>/<app>/secret.sops.yaml

# Decrypt (for inspection only, never commit decrypted)
sops -d cluster/apps/<ns>/<app>/secret.sops.yaml
```

SOPS uses the GnuPG keys defined in `.sops.yaml`. Two key fingerprints are configured (primary + backup).

**⚠️ Never let a full decrypted secrets file reach your output.** When you only need one value
(e.g. `SECRET_APEX_DOMAIN`), pipe directly and only that one way:

```bash
sops -d cluster/base/cluster-secrets.sops.yaml | grep SECRET_APEX_DOMAIN
```

- Never add `2>&1`, `1>/dev/null`, or any other stream redirection/swap to a `sops -d` command to
  "debug" why the pipe returned nothing — that class of redirection can route the full decrypted
  YAML to a visible channel instead of suppressing it, dumping every secret in the file into your
  output/context. This has happened before.
- Never redirect `sops -d` output to a file (including scratch/temp paths) — that materializes
  plaintext secrets on disk.
- If `sops -d ... | grep <KEY>` returns nothing (e.g. because the GPG passphrase prompt can't be
  answered non-interactively), do **not** retry with a different redirection to see what went
  wrong. Stop and ask the user to paste the specific value directly instead.
- If a full decrypted file ever does end up in your output despite these rules, stop immediately,
  tell the user exactly which keys were exposed (not their values, to avoid repeating them), and
  recommend rotating every exposed credential.

## Dependency Updates

Renovate bot automatically opens PRs for Helm chart and container image updates. Labels: `renovate/image`, `renovate/helm`, `dep/major`, `dep/minor`, `dep/patch`. No manual update process needed for tracked dependencies.
