#!/usr/bin/env bash
# Génère report.md à partir des fichiers result-<id>.json produits par Trivy.
# Variables : IMAGES (JSON [{"id":"0","image":"repo:tag"}]), RESULTS_DIR (défaut : results)
set -euo pipefail

RESULTS_DIR="${RESULTS_DIR:-results}"
MAX_DETAILS=20

count() {
  jq --arg sev "$2" '[.Results[]?.Vulnerabilities[]? | select(.Severity == $sev)] | length' "$1"
}

images=$(jq -r '.[] | [.id, .image] | @tsv' <<<"$IMAGES")
total=0
table=""
details=""

while IFS=$'\t' read -r id image; do
  file="$RESULTS_DIR/result-$id.json"
  if [ ! -s "$file" ]; then
    table+="| \`$image\` | ⚠️ scan en échec | | | |"$'\n'
    continue
  fi

  critical=$(count "$file" CRITICAL)
  high=$(count "$file" HIGH)
  medium=$(count "$file" MEDIUM)
  low=$(count "$file" LOW)
  total=$((total + critical + high + medium + low))
  table+="| \`$image\` | $critical | $high | $medium | $low |"$'\n'

  if [ $((critical + high)) -gt 0 ]; then
    details+="<details><summary>🐳 $image</summary>"$'\n\n'
    details+=$(jq -r --argjson max "$MAX_DETAILS" '
      [.Results[]?.Vulnerabilities[]? | select(.Severity == "CRITICAL" or .Severity == "HIGH")]
      | sort_by(if .Severity == "CRITICAL" then 0 else 1 end) as $all
      | ($all[:$max][] |
          "- **\(.Severity)** `\(.PkgName)` \(.InstalledVersion) → \(.FixedVersion) ([\(.VulnerabilityID)](\(.PrimaryURL // ("https://avd.aquasec.com/nvd/" + (.VulnerabilityID | ascii_downcase)))))"),
        (if ($all | length) > $max then "- … et \(($all | length) - $max) autre(s)" else empty end)
    ' "$file")
    details+=$'\n\n</details>\n\n'
  fi
done <<<"$images"

{
  echo "# 🔍 Trivy Scan Report"
  echo
  echo "Seules les failles avec un correctif disponible sont comptées."
  echo
  echo "| Image | 🔴 Critical | 🔶 High | 🟡 Medium | 🟢 Low |"
  echo "|---|---|---|---|---|"
  printf '%s' "$table"
  echo
  if [ "$total" -eq 0 ] && [[ "$table" != *"scan en échec"* ]]; then
    echo "✅ Aucune faille corrigeable détectée."
  fi
  printf '%s' "$details"
} >report.md
