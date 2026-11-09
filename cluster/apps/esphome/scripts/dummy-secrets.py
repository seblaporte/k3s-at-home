#!/usr/bin/env python3
"""Écrit un secrets.yaml factice couvrant tous les `!secret` utilisés (CI sans clé PGP).

Les clés API doivent être du base64 de 32 octets, les `*_mac` une adresse MAC ; le reste accepte une chaîne quelconque.
  scripts/dummy-secrets.py > secrets.yaml
"""

import base64
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
names = sorted(
    {m for f in (ROOT / "devices").glob("*.yaml") for m in re.findall(r"!secret\s+(\w+)", f.read_text())}
)
for name in names:
    if name.endswith("api_key"):
        value = base64.b64encode(hashlib.sha256(name.encode()).digest()).decode()
    elif name.endswith("_mac"):
        value = "AA:BB:CC:DD:EE:FF"
    else:
        value = f"dummy-{name}"
    print(f'{name}: "{value}"')
