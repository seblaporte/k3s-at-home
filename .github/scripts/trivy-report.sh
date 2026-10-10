#!/usr/bin/env bash
# Génère report.md à partir des fichiers result-<id>.json (nouvelle image) et result-<id>-old.json
# (ancienne image, optionnel) produits par Trivy.
# Variables : IMAGES (JSON [{"id":"0","image":"repo:tag","old":"repo:oldtag"}]), RESULTS_DIR (défaut : results)
set -euo pipefail

RESULTS_DIR="${RESULTS_DIR:-results}"
MAX_DETAILS=20

count() {
  jq --arg sev "$2" '[.Results[]?.Vulnerabilities[]? | select(.Severity == $sev)] | length' "$1"
}

# Failles d'un rapport, au format compact : [{id, pkg, sev, inst, fix, url}]
vulns() {
  jq -c '[.Results[]?.Vulnerabilities[]? | {
    id: .VulnerabilityID, pkg: .PkgName, sev: .Severity, inst: .InstalledVersion, fix: .FixedVersion,
    url: (.PrimaryURL // ("https://avd.aquasec.com/nvd/" + (.VulnerabilityID | ascii_downcase)))
  }]' "$1"
}

# Lignes Markdown des failles critiques/hautes d'une liste JSON (tronquée à MAX_DETAILS)
list_lines() {
  jq -r --argjson max "$MAX_DETAILS" '
    [.[] | select(.sev == "CRITICAL" or .sev == "HIGH")]
    | sort_by(if .sev == "CRITICAL" then 0 else 1 end) as $all
    | ($all[:$max][] | "- **\(.sev)** `\(.pkg)` \(.inst) → \(.fix) ([\(.id)](\(.url)))"),
      (if ($all | length) > $max then "- … et \(($all | length) - $max) autre(s)" else empty end)
  ' <<<"$1"
}

# Cellule de tableau : "ancien → nouveau" si une comparaison existe, sinon "nouveau"
cell() {
  if [ -n "$2" ]; then echo "$2 → $1"; else echo "$1"; fi
}

images=$(jq -r '.[] | [.id, .image, (.old // "")] | @tsv' <<<"$IMAGES")
total=0
table=""
details=""

while IFS=$'\t' read -r id image old; do
  file="$RESULTS_DIR/result-$id.json"
  old_file="$RESULTS_DIR/result-$id-old.json"

  if [ ! -s "$file" ]; then
    table+="| \`$image\` | ⚠️ scan en échec | | | |"$'\n'
    continue
  fi

  compare=false
  if [ -n "$old" ] && [ -s "$old_file" ]; then
    compare=true
  fi

  critical=$(count "$file" CRITICAL)
  high=$(count "$file" HIGH)
  medium=$(count "$file" MEDIUM)
  low=$(count "$file" LOW)
  total=$((total + critical + high + medium + low))

  label="\`$image\`"
  o_critical=""
  o_high=""
  o_medium=""
  o_low=""
  if $compare; then
    label+="<br>↳ remplace \`$old\`"
    o_critical=$(count "$old_file" CRITICAL)
    o_high=$(count "$old_file" HIGH)
    o_medium=$(count "$old_file" MEDIUM)
    o_low=$(count "$old_file" LOW)
  elif [ -n "$old" ]; then
    label+="<br>↳ remplace \`$old\` (scan en échec, pas de comparaison)"
  fi
  table+="| $label | $(cell "$critical" "$o_critical") | $(cell "$high" "$o_high") | $(cell "$medium" "$o_medium") | $(cell "$low" "$o_low") |"$'\n'

  if $compare; then
    diff=$(jq -n -c --argjson old "$(vulns "$old_file")" --argjson new "$(vulns "$file")" '
      def key: "\(.id)|\(.pkg)";
      ($old | map({(key): true}) | add // {}) as $oset
      | ($new | map({(key): true}) | add // {}) as $nset
      | {
          fixed: [$old[] | select($nset[key] | not)],
          remaining: [$new[] | select($oset[key])],
          added: [$new[] | select($oset[key] | not)]
        }')
    n_fixed=$(jq '.fixed | length' <<<"$diff")
    n_remaining=$(jq '.remaining | length' <<<"$diff")
    n_added=$(jq '.added | length' <<<"$diff")
    fixed_ch=$(jq '[.fixed[] | select(.sev == "CRITICAL" or .sev == "HIGH")] | length' <<<"$diff")

    sections=""
    for section in fixed:"✅ Corrigées" added:"🆕 Nouvelles" remaining:"⏳ Restantes"; do
      key=${section%%:*}
      title=${section#*:}
      lines=$(list_lines "$(jq -c ".$key" <<<"$diff")")
      if [ -n "$lines" ]; then
        sections+="**$title (critiques et hautes)**"$'\n\n'"$lines"$'\n\n'
      fi
    done
    if [ -n "$sections" ]; then
      details+="<details><summary>🐳 $image : $n_fixed corrigée(s) dont $fixed_ch critique(s)/haute(s), $n_remaining restante(s), $n_added nouvelle(s)</summary>"$'\n\n'
      details+="$sections</details>"$'\n\n'
    fi
  elif [ $((critical + high)) -gt 0 ]; then
    details+="<details><summary>🐳 $image</summary>"$'\n\n'
    details+="$(list_lines "$(vulns "$file")")"
    details+=$'\n\n</details>\n\n'
  fi
done <<<"$images"

{
  echo "# 🔍 Trivy Scan Report"
  echo
  echo "Seules les failles avec un correctif disponible sont comptées. Quand l'ancien tag est connu, les compteurs sont affichés « ancien → nouveau »."
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
