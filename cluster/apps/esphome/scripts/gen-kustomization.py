#!/usr/bin/env python3
"""Génère kustomization.yaml à partir de devices/ et components/.

kustomize ne sait pas lister un dossier : chaque fichier doit être déclaré.
  scripts/gen-kustomization.py          écrit kustomization.yaml
  scripts/gen-kustomization.py --check  échoue si le fichier n'est pas à jour (CI)

Objets produits en plus de namespace/helm-release/probe (noms fixes, sans suffixe de hash) :
  ConfigMap esphome-devices               une clé par devices/*.yaml → /config
  ConfigMap esphome-component-<nom>       une clé par fichier de components/<nom>/ → /components/<nom>
  Secret    esphome-secrets               secret.sops.yaml (déchiffré par Flux) → /config/secrets.yaml

Tous deux portent l'annotation `substitute: disabled` : le postBuild de la Kustomization `apps`
remplacerait sinon les `${...}` d'ESPHome par du vide.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "kustomization.yaml"


def render() -> str:
    lines = [
        "---",
        "# Généré par scripts/gen-kustomization.py : ne pas modifier à la main (task esphome:kustomization).",
        "apiVersion: kustomize.config.k8s.io/v1beta1",
        "kind: Kustomization",
        "namespace: esphome",
        "resources:",
        "  - namespace.yaml",
        "  - helm-release.yaml",
        "  - probe.yaml",
        "  - secret.sops.yaml",
        "generatorOptions:",
        "  disableNameSuffixHash: true",
        "  labels:",
        "    app.kubernetes.io/part-of: esphome-config",
        "  annotations:",
        "    # Les ${...} d'ESPHome ne doivent pas être substitués par Flux (postBuild de `apps`)",
        "    kustomize.toolkit.fluxcd.io/substitute: disabled",
        "patches:",
        "  - patch: |-",
        "      apiVersion: v1",
        "      kind: Secret",
        "      metadata:",
        "        name: esphome-secrets",
        "        namespace: esphome",
        "        annotations:",
        "          kustomize.toolkit.fluxcd.io/substitute: disabled",
        "configMapGenerator:",
        "  - name: esphome-devices",
        "    files:",
    ]
    lines += [f"      - {p.relative_to(ROOT)}" for p in sorted((ROOT / "devices").glob("*.yaml"))]
    # ESPHome écrit /config/.gitignore à la compilation s'il est absent : /config étant en
    # lecture seule, on le fournit (nommé esphome.gitignore : Flux exclut les .gitignore du dépôt)
    lines.append("      - .gitignore=devices/esphome.gitignore")
    components = ROOT / "components"
    for comp in sorted(p for p in components.glob("*") if p.is_dir()) if components.exists() else []:
        files = sorted(
            f for f in comp.iterdir() if f.is_file() and f.suffix in {".py", ".h", ".cpp", ".c", ".hpp"}
        )
        if not files:
            continue
        lines += [f"  - name: esphome-component-{comp.name.replace('_', '-')}", "    files:"]
        lines += [f"      - {f.name}={f.relative_to(ROOT)}" for f in files]
    return "\n".join(lines) + "\n"


def main() -> int:
    content = render()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != content:
            print("kustomization.yaml n'est pas à jour : lancer `task esphome:kustomization`", file=sys.stderr)
            return 1
        print("kustomization.yaml à jour")
        return 0
    OUT.write_text(content)
    print(f"{OUT.name} généré")
    return 0


if __name__ == "__main__":
    sys.exit(main())
