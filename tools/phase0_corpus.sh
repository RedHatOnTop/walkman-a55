#!/bin/bash
#
# Pull a compact, text-only corpus out of the stock system rootfs for the
# phase 0 architecture map: init scripts, file inventory, and for each Sony
# binary its library deps, C++ classes, paths, properties and Qt hints.
#
# Usage: phase0_corpus.sh ROOTFS RAMDISK OUT
#
set -u

ROOT="$1"
RD="$2"
OUT="$3"

section() { printf '\n\n######## %s\n' "$1" >> "$OUT"; }

: > "$OUT"

section "init.rc (boot ramdisk)"
cat "$RD/init.rc" >> "$OUT"
for f in init.hagoromo.rc init.usbcfg.rc init.usbcfgdmp.rc ueventd.icx.rc; do
    section "$f"
    cat "$RD/$f" >> "$OUT"
done

section "file inventory (path size)"
( cd "$ROOT" && find bin xbin usr vendor lib etc -type f -printf '%p %s\n' 2>/dev/null \
    | grep -v -E '^vendor/sony/(qml|translations)/|^usr/share/(zoneinfo|fonts)/' | sort ) >> "$OUT"

section "symlinks"
( cd "$ROOT" && find . -type l -printf '%p -> %l\n' | sort ) >> "$OUT"

section "kernel modules"
for m in "$ROOT"/lib/modules/*.ko; do
    printf '%s: %s\n' "$(basename "$m")" \
        "$(strings -n 6 "$m" | grep -E '^(description|author|depends|parm)=' | tr '\n' ' ')" >> "$OUT"
done

section "small text configs under etc and vendor/sony"
for f in $(cd "$ROOT" && find etc vendor/sony -maxdepth 2 -type f \( -name '*.conf' -o -name '*.json' -o -name '*.ini' -o -name '*.appcfg' -o -name '*.sh' -o -name '*.rc' -o -name '*.txt' -o -name '*.xml' \) -size -8k ! -path 'etc/fonts/*' 2>/dev/null); do
    printf '\n--- %s\n' "$f" >> "$OUT"
    head -c 4000 "$ROOT/$f" >> "$OUT"
done

elf_digest() {
    local f="$1" rel="${1#$ROOT/}"
    printf '\n=== %s (%s bytes)\n' "$rel" "$(stat -c %s "$f")" >> "$OUT"
    readelf -d "$f" 2>/dev/null | sed -n 's/.*(\(NEEDED\|SONAME\)).*\[\(.*\)\]/\1 \2/p' | tr '\n' ' ' >> "$OUT"
    echo >> "$OUT"
    local s
    s="$(strings -n 4 "$f")"
    echo "classes:" >> "$OUT"
    echo "$s" | grep '^_Z' | c++filt 2>/dev/null \
        | grep -o -E '\b(pst|icx|sony|hgrm|Hgrm)[A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)+' \
        | sed -E 's/::[a-z_][A-Za-z0-9_]*$//; s/::~?[A-Z][A-Za-z0-9_]*\(.*$//' \
        | sort | uniq -c | sort -rn | head -15 | awk '{printf "  %s(%s)", $2, $1} END {print ""}' >> "$OUT"
    echo "paths/props/ipc:" >> "$OUT"
    echo "$s" | grep -o -E '(/(dev|sys|proc|system|data|var|db|contents|tmp|opt[123]|factory|mnt|etc|usr)/[A-Za-z0-9_./@%-]+|\b(sys|persist|ro|init\.svc|service)\.[a-z0-9_.]+|qrc:/[A-Za-z0-9_./-]+|[A-Za-z0-9_]+\.qml|[A-Z][A-Za-z]+Service(Fw)?\b)' \
        | grep -v -E '\.(png|jpg|bmp)$' | sort -u | head -45 | tr '\n' ' ' >> "$OUT"
    echo >> "$OUT"
}

section "Sony binaries and libraries"
for f in "$ROOT"/vendor/sony/bin/* "$ROOT"/vendor/sony/lib/*.so*; do
    file -b "$f" | grep -q ELF && elf_digest "$f"
done

section "other ELF binaries (/bin /xbin /usr/bin): deps only"
for f in "$ROOT"/bin/* "$ROOT"/xbin/* "$ROOT"/usr/bin/* "$ROOT"/usr/local/bin/*; do
    [ -f "$f" ] || continue
    file -b "$f" | grep -q ELF || continue
    printf '%s: %s\n' "${f#$ROOT/}" \
        "$(readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')" >> "$OUT"
done

section "Qt plugins and libs"
( cd "$ROOT" && find vendor/sony/plugins lib -name '*Qt5*' -o -path '*plugins*' -type f ) | sort >> "$OUT"
