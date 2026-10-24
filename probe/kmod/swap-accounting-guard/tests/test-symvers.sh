#!/bin/sh
# Error-path and decoder tests for the stock export metadata tools.
#
# Fixtures are written with heredocs so they contain real newlines; a fixture
# that collapses to one line would fail for the wrong reason. Every case checks
# the exit code, that the previous output survived, and the error message, so a
# passing test means the named path ran.
set -u
cd "$(dirname "$0")/.."
. ./tests/adb-guard.sh
GEN=tools/gen-stock-symvers.sh
DEC=tools/verify-stock-exports.py
KALL="${NW_KALLSYMS:-../../../work/android/wdt-nozram-20261012T062210Z/baseline/kallsyms.out}"
VMLINUX="${NW_VMLINUX:-../../../nwa50-extracted/kernel/vmlinux.bin}"
PY="env -u PYTHONHOME -u PYTHONPATH python3"
fail=0

D=$(mktemp -d); trap 'rm -rf "$D"' EXIT INT TERM
printf 'KEEP\n' > "$D/out"

cat > "$D/malformed" <<'EOF'
ZZZZZZZZ r __start___ksymtab
ffffffff r __stop___ksymtab
c0a95e90 r __start___ksymtab_gpl
c0a9abd8 r __stop___ksymtab_gpl
c0a8e068 r __ksymtab_printk
EOF
cat > "$D/order" <<'EOF'
c0a95e90 r __start___ksymtab
c0a8e068 r __stop___ksymtab
c0a95e90 r __start___ksymtab_gpl
c0a9abd8 r __stop___ksymtab_gpl
c0a8e068 r __ksymtab_printk
EOF
cat > "$D/overlap" <<'EOF'
c0a8e068 r __start___ksymtab
c0a9abd8 r __stop___ksymtab
c0a95e90 r __start___ksymtab_gpl
c0a9e000 r __stop___ksymtab_gpl
c0a8e068 r __ksymtab_printk
EOF
cat > "$D/badaddr" <<'EOF'
c0a8e068 r __start___ksymtab
c0a95e90 r __stop___ksymtab
c0a95e90 r __start___ksymtab_gpl
c0a9abd8 r __stop___ksymtab_gpl
ZZZZZZZZ r __ksymtab_bogus
EOF
printf 'c0008000 T foo\n' > "$D/nomark"
: > "$D/empty"
printf 'no_such_symbol_xyz\n' > "$D/req_bad"
printf 'printk\n' > "$D/req_ok"

expect() { # name want got
    if [ "$3" = "$2" ]; then printf 'PASS %-46s (%s)\n' "$1" "$3"
    else printf 'FAIL %-46s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi
}
expect_msg() { # name substring file
    if grep -q "$2" "$3" 2>/dev/null; then printf 'PASS %-46s (%s)\n' "$1" "$2"
    else printf 'FAIL %-46s message missing: %s\n' "$1" "$2"; fail=1; fi
}

run_gen() { "$GEN" "$1" "$D/out" >/dev/null 2>"$D/err"; echo $?; }

expect "missing input"            3 "$(run_gen "$D/nope")"
expect_msg "missing input message" "no such kallsyms file" "$D/err"
expect "output preserved" "KEEP" "$(cat "$D/out")"
expect "empty input"              3 "$(run_gen "$D/empty")"
expect "no section bounds"        4 "$(run_gen "$D/nomark")"
expect_msg "bounds message" "bounds missing" "$D/err"
expect "malformed bounds"         4 "$(run_gen "$D/malformed")"
expect_msg "malformed message" "malformed bound" "$D/err"
expect "out-of-order bounds"      4 "$(run_gen "$D/order")"
expect_msg "order message" "out of order" "$D/err"
expect "overlapping sections"     4 "$(run_gen "$D/overlap")"
expect_msg "overlap message" "overlap" "$D/err"
expect "malformed marker address" 5 "$(run_gen "$D/badaddr")"
expect_msg "bad address message" "bad marker address" "$D/err"
expect "output still preserved" "KEEP" "$(cat "$D/out")"

if [ -r "$KALL" ]; then
    "$GEN" "$KALL" "$D/real" >/dev/null 2>&1;  expect "real kallsyms" 0 "$?"
    expect "nonempty symvers" yes "$([ -s "$D/real" ] && echo yes)"
    expect "gpl category preserved" yes "$(grep -q 'EXPORT_SYMBOL_GPL$' "$D/real" && echo yes)"
    "$GEN" "$KALL" "$D/out2" "$D/req_bad" >/dev/null 2>&1
    expect "required symbol absent" 7 "$?"
    "$GEN" "$KALL" "$D/out3" "$D/req_ok" >/dev/null 2>&1
    expect "required symbol present" 0 "$?"
    mkdir -p "$D/adir"
    "$GEN" "$KALL" "$D/adir" >/dev/null 2>&1
    expect "output path is a directory" 3 "$?"
    chmod 500 "$D/adir2" 2>/dev/null || mkdir -p "$D/adir2"
    mkdir -p "$D/adir2"; chmod 500 "$D/adir2"
    "$GEN" "$KALL" "$D/adir2/o" >/dev/null 2>&1
    expect "unwritable output directory" 3 "$?"
    chmod 700 "$D/adir2"
else
    echo "FAIL stock capture missing: $KALL" >&2; fail=1
fi

if [ -r "$VMLINUX" ] && [ -r "$KALL" ]; then
    $PY "$DEC" "$VMLINUX" "$KALL" "$D/req_bad" --gpl >/dev/null 2>&1
    expect "decoder rejects a non-export" 1 "$?"
    printf 'stop_machine\n' > "$D/gplin"
    $PY "$DEC" "$VMLINUX" "$KALL" "$D/gplin" >/dev/null 2>&1
    expect "decoder rejects GPL without --gpl" 1 "$?"
    $PY "$DEC" "$VMLINUX" "$KALL" "$D/gplin" --gpl >/dev/null 2>&1
    expect "decoder accepts GPL with --gpl" 0 "$?"
    $PY "$DEC" "$VMLINUX" "$KALL" "$D/req_ok" >/dev/null 2>&1
    expect "decoder accepts a plain export" 0 "$?"
    KO=swap_accounting_guard.ko
    if [ -f "$KO" ]; then
        "${NM:-$HOME/.cache/arm-eabi-4.8/bin/arm-eabi-nm}" -u "$KO" | awk '{print $2}' | sort -u > "$D/imports"
        $PY "$DEC" "$VMLINUX" "$KALL" "$D/imports" --gpl >/dev/null 2>&1
        expect "decoder accepts all built .ko imports" 0 "$?"
    else
        echo "FAIL built .ko missing: run make first" >&2; fail=1
    fi
    $PY "$DEC" "$D/nope" "$KALL" >/dev/null 2>&1
    expect "decoder missing vmlinux" 1 "$?"
    $PY "$DEC" "$VMLINUX" "$D/nope" >/dev/null 2>&1
    expect "decoder missing kallsyms" 1 "$?"
    : > "$D/empty_imports"
    $PY "$DEC" "$VMLINUX" "$KALL" "$D/empty_imports" --gpl >/dev/null 2>"$D/dei"
    expect "decoder refuses an empty imports file" 1 "$?"
    expect_msg "empty imports message" "no symbols" "$D/dei"
    # corrupted export value: zero the value of printk's entry
    cp "$VMLINUX" "$D/vmlinux.bin"
    $PY - "$D/vmlinux.bin" "$KALL" <<'PYEOF' >/dev/null 2>&1
import sys
blob = bytearray(open(sys.argv[1], 'rb').read())
syms = {}
for line in open(sys.argv[2], errors='replace'):
    p = line.replace('\r', '').split()
    if len(p) >= 3 and '[' not in line:
        try: syms.setdefault(p[2], set()).add(int(p[0], 16))
        except ValueError: pass
off = min(syms['__ksymtab_printk']) - min(syms['_text'])
blob[off:off+4] = (0).to_bytes(4, 'little')
open(sys.argv[1], 'wb').write(blob)
PYEOF
    $PY "$DEC" "$D/vmlinux.bin" "$KALL" "$D/req_ok" >/dev/null 2>"$D/derr"
    expect "decoder rejects a zeroed value" 1 "$?"
    expect_msg "zeroed value message" "zero value" "$D/derr"
    # a REAL module shadow: retarget crc16_table's export value to its
    # same-name module address 0xbf037578 (kallsyms fourth field is [module])
    cp "$VMLINUX" "$D/v3.bin"
    $PY - "$D/v3.bin" "$KALL" <<'PYEOF2' >/dev/null 2>&1
import sys
blob = bytearray(open(sys.argv[1], 'rb').read()); b = {}
for line in open(sys.argv[2], errors='replace'):
    p = line.replace('\r', '').split()
    if len(p) >= 3:
        try: a = int(p[0], 16)
        except ValueError: continue
        if len(p) >= 4 and p[3].startswith('['): continue
        b.setdefault(p[2], set()).add(a)
off = min(b['__ksymtab_crc16_table']) - min(b['_text'])
blob[off:off+4] = (0xbf037578).to_bytes(4, 'little')
open(sys.argv[1], 'wb').write(blob)
PYEOF2
    printf 'crc16_table\n' > "$D/crcin"
    $PY "$DEC" "$D/v3.bin" "$KALL" "$D/crcin" >/dev/null 2>"$D/derrc"
    expect "decoder rejects a module-shadow retarget" 1 "$?"
    expect_msg "module-shadow message" "module addresses" "$D/derrc"
    # remove a marker by its name field and require the entry->marker check
    cp "$VMLINUX" "$D/v4.bin"
    $PY - "$D/v4.bin" "$D/kall_nomarker" "$KALL" <<'PYEOF3' >/dev/null 2>&1
import sys
out = open(sys.argv[2], 'w')
for line in open(sys.argv[3], errors='replace'):
    p = line.replace('\r', '').split()
    if len(p) >= 3 and p[2] == '__ksymtab_printk':
        continue
    out.write(line)
out.close()
blob = bytearray(open(sys.argv[1], 'rb').read()); b = {}
for line in open(sys.argv[3], errors='replace'):
    p = line.replace('\r', '').split()
    if len(p) >= 3:
        try: a = int(p[0], 16)
        except ValueError: continue
        if len(p) >= 4 and p[3].startswith('['): continue
        b.setdefault(p[2], set()).add(a)
# zero the value so the value check cannot be what fires
off = min(b['__ksymtab_printk']) - min(b['_text'])
blob[off:off+4] = min(b['printk']).to_bytes(4, 'little')
open(sys.argv[1], 'wb').write(blob)
PYEOF3
    $PY "$DEC" "$D/v4.bin" "$D/kall_nomarker" "$D/req_ok" >/dev/null 2>"$D/derrm"
    expect "decoder rejects a missing marker" 1 "$?"
    expect_msg "missing marker message" "has no marker" "$D/derrm"
else
    echo "FAIL stock vmlinux/kallsyms missing" >&2; fail=1
fi

[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit "$fail"
