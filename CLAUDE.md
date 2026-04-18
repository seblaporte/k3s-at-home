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

**Helm charts**: Primary sources are `k8s-at-home`, `bjw-s` (app-template), `bitnami`, `jetstack`, and `traefik` repositories — all defined as `HelmRepository` objects in `cluster/base/flux-system/charts/`.

**SOPS encryption**: Only `data` and `stringData` fields are encrypted (per `.sops.yaml`). Files containing secrets must be named `*.sops.yaml`. Never commit unencrypted secrets.

**Ingress**: Traefik is the ingress controller. TLS via cert-manager with Let's Encrypt (Cloudflare DNS-01 challenge). Use staging issuer for testing.

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

## Dependency Updates

Renovate bot automatically opens PRs for Helm chart and container image updates. Labels: `renovate/image`, `renovate/helm`, `dep/major`, `dep/minor`, `dep/patch`. No manual update process needed for tracked dependencies.
