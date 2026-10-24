#!/bin/sh
# Build a Module.symvers from the stock kernel's own export metadata.
#
# Source of truth: a captured stock /proc/kallsyms. Each exported symbol has a
# __ksymtab_<name> marker whose entry address falls inside one of the export
# sections, which is how the GPL category is recovered:
#   __ksymtab -> EXPORT_SYMBOL, __ksymtab_gpl[_future] -> EXPORT_SYMBOL_GPL
# CONFIG_MODVERSIONS is off, so a zero CRC is legitimate for a validated export.
#
#   tools/gen-stock-symvers.sh <kallsyms-file> <out-file> [required-symbols-file]
#
# Bounds are validated (well-formed 32-bit, 8-byte aligned, ordered,
# non-overlapping, complete pairs) before anything is classified, and each
# marker address must parse. The output is written to a temporary file and
# renamed only after every check passes, so a failure leaves the previous output
# untouched. No pipeline hides a status.
set -eu

# Reproducible output: the sort order of the export rows must not depend on the
# caller's locale. Sorting under C is what makes the exact-byte comparison
# against a previously generated Module.symvers meaningful.
LC_ALL=C
export LC_ALL
LANG=C
export LANG

[ $# -ge 2 ] && [ $# -le 3 ] || {
    echo "usage: $0 <kallsyms-file> <out-file> [required-symbols-file]" >&2; exit 2; }
IN="$1"; OUT="$2"; REQ="${3:-}"

[ -f "$IN" ] || { echo "$0: no such kallsyms file: $IN" >&2; exit 3; }
[ -r "$IN" ] || { echo "$0: kallsyms file not readable: $IN" >&2; exit 3; }
[ -s "$IN" ] || { echo "$0: kallsyms file is empty: $IN" >&2; exit 3; }
if [ -n "$REQ" ]; then
    { [ -f "$REQ" ] && [ -r "$REQ" ] && [ -s "$REQ" ]; } || {
        echo "$0: required-symbols file unusable: $REQ" >&2; exit 3; }
fi
OUTDIR="$(dirname "$OUT")"
{ [ -d "$OUTDIR" ] && [ -w "$OUTDIR" ]; } || {
    echo "$0: output directory not writable: $OUTDIR" >&2; exit 3; }
[ -d "$OUT" ] && { echo "$0: output path is a directory: $OUT" >&2; exit 3; }

TMP="$OUT.tmp.$$"; RAW="$OUT.raw.$$"; RAWIN="$OUT.rawin.$$"
trap 'rm -f "$TMP" "$RAW" "$RAWIN"' EXIT INT TERM

tr -d '\r' < "$IN" > "$RAWIN"

BOUNDS=$(awk '
  $3 == "__start___ksymtab"            { s = $1 }
  $3 == "__stop___ksymtab"             { e = $1 }
  $3 == "__start___ksymtab_gpl"        { gs = $1 }
  $3 == "__stop___ksymtab_gpl"         { ge = $1 }
  $3 == "__start___ksymtab_gpl_future" { fs = $1 }
  $3 == "__stop___ksymtab_gpl_future"  { fe = $1 }
  END { printf "%s %s %s %s %s %s\n", s, e, gs, ge, fs, fe }' "$RAWIN")
set -- $BOUNDS
S="${1:-}"; E="${2:-}"; GS="${3:-}"; GE="${4:-}"; FS="${5:-}"; FE="${6:-}"

[ -n "$S" ] && [ -n "$E" ] && [ -n "$GS" ] && [ -n "$GE" ] || {
    echo "$0: export section bounds missing from $IN" >&2; exit 4; }
for v in "$S" "$E" "$GS" "$GE"; do
    case "$v" in
        *[!0-9a-fA-F]*|"") echo "$0: malformed bound '$v' in $IN" >&2; exit 4 ;;
    esac
    [ "${#v}" -eq 8 ] || { echo "$0: bound '$v' is not a 32-bit address" >&2; exit 4; }
done
for v in "$FS" "$FE"; do
    [ -n "$v" ] || continue
    case "$v" in *[!0-9a-fA-F]*) echo "$0: malformed gpl_future bound '$v'" >&2; exit 4 ;; esac
done

awk -v s="$S" -v e="$E" -v gs="$GS" -v ge="$GE" -v fs="${FS:-0}" -v fe="${FE:-0}" '
  function d(h,   i,c,v){ v=0; for(i=1;i<=length(h);i++){ c=tolower(substr(h,i,1))
      v = v*16 + index("0123456789abcdef", c) - 1 } return v }
  BEGIN {
      S=d(s); E=d(e); GS=d(gs); GE=d(ge); FSd=d(fs); FEd=d(fe)
      if (S%8 || E%8 || GS%8 || GE%8) { print "bounds not 8-byte aligned" > "/dev/stderr"; bad=1 }
      if (S >= E)  { print "__ksymtab bounds out of order or empty" > "/dev/stderr"; bad=1 }
      if (GS >= GE) { print "__ksymtab_gpl bounds out of order or empty" > "/dev/stderr"; bad=1 }
      if (E > GS)  { print "export sections overlap" > "/dev/stderr"; bad=1 }
      if (FEd > FSd && (FSd%8 || FEd%8 || GE > FSd)) { print "gpl_future bounds inconsistent" > "/dev/stderr"; bad=1 }
      if (bad) exit 4
  }' || { echo "$0: inconsistent section bounds in $IN" >&2; exit 4; }

awk -v s="$S" -v e="$E" -v gs="$GS" -v ge="$GE" -v fs="${FS:-0}" -v fe="${FE:-0}" '
  function d(h,   i,c,v){ v=0; for(i=1;i<=length(h);i++){ c=tolower(substr(h,i,1))
      v = v*16 + index("0123456789abcdef", c) - 1 } return v }
  BEGIN { S=d(s); E=d(e); GS=d(gs); GE=d(ge); FSd=d(fs); FEd=d(fe) }
  $3 ~ /^__ksymtab_/ {
      a = d($1)
      if (a < 0) { print "bad marker address" > "/dev/stderr"; bad = 1; next }
      name = $3
      sub(/^__ksymtab_gpl_future_/, "", name)
      sub(/^__ksymtab_gpl_/, "", name)
      sub(/^__ksymtab_/, "", name)
      if (name == "") next
      if (a >= S && a < E)                       cat = "EXPORT_SYMBOL"
      else if (a >= GS && a < GE)                cat = "EXPORT_SYMBOL_GPL"
      else if (FEd > FSd && a >= FSd && a < FEd) cat = "EXPORT_SYMBOL_GPL"
      else { skipped++; next }
      print "0x00000000\t" name "\tvmlinux\t" cat
  }
  END {
      if (bad) exit 5
      if (skipped > 0)
          print "note: " skipped " markers outside the export sections were skipped" > "/dev/stderr"
  }' "$RAWIN" > "$RAW" || { echo "$0: parsing failed for $IN" >&2; exit 5; }
rm -f "$RAWIN"

sort -u "$RAW" > "$TMP" || { echo "$0: sort failed" >&2; exit 5; }
COUNT=$(wc -l < "$TMP")
[ "$COUNT" -gt 0 ] || { echo "$0: no exports found in $IN" >&2; exit 6; }

if [ -n "$REQ" ]; then
    MISSING=$(awk 'NF' "$REQ" | while read -r sym; do
        grep -q "	$sym	" "$TMP" || echo "$sym"
    done)
    [ -z "$MISSING" ] || {
        echo "$0: required symbols missing from the export set:" >&2
        echo "$MISSING" >&2; exit 7; }
fi

mv "$TMP" "$OUT" 2>/dev/null || { echo "$0: cannot replace $OUT" >&2; exit 8; }
[ -f "$OUT" ] || { echo "$0: replacement did not produce a file: $OUT" >&2; exit 8; }
trap - EXIT INT TERM
rm -f "$RAW"
echo "wrote $COUNT stock exports to $OUT" >&2
