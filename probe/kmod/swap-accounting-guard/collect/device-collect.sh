#!/system/xbin/busybox sh
set -eu
export LC_ALL=C
BB="${BB-/system/xbin/busybox}"
PROC_ROOT="${PROC_ROOT:-/proc}"
TMP_ROOT="${TMP_ROOT:-/tmp}"
INIT_PID_FILE="${INIT_PID_FILE:-/tmp/aroot/init.pid}"
PIVOTINIT="${PIVOTINIT:-/tmp/pivotinit}"
BUFFER_DIR=''; SCAN=1
fail() { printf 'COLLECT_ERROR=%s\n' "$*" >&2; exit 1; }
[ "$#" -le 1 ] || fail arguments
case "${1:-}" in '') ;; --no-smaps) SCAN=0 ;; *) fail arguments ;; esac
cleanup() {
    [ -n "$BUFFER_DIR" ] || return 0
    $BB rm -f "$BUFFER_DIR/current" "$BUFFER_DIR/max" "$BUFFER_DIR/summaries" || return 1
    $BB rmdir "$BUFFER_DIR" || return 1
    BUFFER_DIR=''
}
finish() {
    status=$?; trap - 0
    if ! cleanup; then printf 'COLLECT_ERROR=cleanup\n' >&2; status=1; fi
    exit "$status"
}
trap 'finish' 0
trap 'fail interrupted' 2
trap 'fail interrupted' 15
mread() {
    label=$1; shift
    value=$("$@" 2>/dev/null) || fail "$label"
    [ -n "$value" ] || fail "empty_$label"
    printf '%s' "$value"
}
UP1=$(mread uptime $BB cut -d' ' -f1 "$PROC_ROOT/uptime") || exit 1
BOOTID=$(mread bootid $BB cat "$PROC_ROOT/sys/kernel/random/boot_id") || exit 1
MEMINFO=$(mread meminfo $BB cat "$PROC_ROOT/meminfo") || exit 1
SWAPS=$(mread swaps $BB cat "$PROC_ROOT/swaps") || exit 1
WDK=$(mread wdk $BB cat "$PROC_ROOT/wdk") || exit 1
printf 'BEGIN_SAMPLE\nUPTIME_START=%s\nBOOT_ID=%s\n' "$UP1" "$BOOTID"
printf 'BEGIN_MEMINFO\n%s\nEND_MEMINFO\n' "$MEMINFO"
printf 'BEGIN_SWAPS\n%s\nEND_SWAPS\n' "$SWAPS"
printf 'BEGIN_WDK\n%s\nEND_WDK\n' "$WDK"
BC=''; BC_RC=not_attempted
if [ -f "$INIT_PID_FILE" ]; then
    if CID=$($BB cat "$INIT_PID_FILE" 2>/dev/null); then
        if BC=$(ANDROID_ROOT=/system "$PIVOTINIT" --join "$CID" /system/bin/getprop sys.boot_completed 2>/dev/null); then
            BC_RC=0
        else
            BC_RC=$?; BC=''
        fi
    else
        BC_RC=$?
    fi
fi
printf 'BOOT_COMPLETED=%s\nBOOT_COMPLETED_RC=%s\nSMAPS_SCAN=%s\n' "$BC" "$BC_RC" "$SCAN"
MAXPID='-'
if [ "$SCAN" -eq 1 ]; then
    tmpfs=$($BB awk -v path="$TMP_ROOT" '$2 == path && $3 == "tmpfs" {print "yes"; exit}' "$PROC_ROOT/mounts") || fail mounts
    [ "$tmpfs" = yes ] || fail tmp_root_not_tmpfs
    BUFFER_DIR=$($BB mktemp -d "$TMP_ROOT/sg-collect.XXXXXX") || fail buffer_directory
    # One awk process avoids the measured per-PID fork cost. Raw copies use the saved buffer, never proc rereads.
    $BB awk -v current="$BUFFER_DIR/current" -v maximum="$BUFFER_DIR/max" '
    function identity(path,pid, value,r,n,f) {
        r=(getline value < path); close(path)
        if(r!=1 || value !~ ("^" pid " ")) return "-"
        sub(/^.*\) /,"",value); n=split(value,f," ")
        return (n>=20 && f[20] ~ /^[0-9]+$/) ? f[20] : "-"
    }
    function scan(path,pid, before,after,name,line,r,n,fields,total,rc,copied,copyrc) {
        before=identity(path "/stat",pid)
        r=(getline name < (path "/comm")); close(path "/comm")
        if(r!=1) name="-"; gsub(/[\r\t]/," ",name)
        printf "%s","" > current
        total=0; n=0
        while((r=(getline line < (path "/smaps")))>0) {
            print line > current; n++
            if(line ~ /^Swap:/) {
                if(split(line,fields," ")!=3 || fields[2] !~ /^[0-9]+$/ || fields[3]!="kB") exit 2
                total+=fields[2]
            }
        }
        rc=(r<0) ? 1 : 0; close(path "/smaps")
        if(close(current)!=0) exit 3
        after=identity(path "/stat",pid)
        printf "PID_SUMMARY=%s start_before=%s start_after=%s smaps_rc=%d swap_kb=%.0f lines=%d comm=%s\n",pid,before,after,rc,total,n,name
        if(rc==0 && n>0 && before!="-" && before==after && (total>best || (total==best && pid+0<bestpid+0))) {
            printf "%s","" > maximum; copied=0
            while((copyrc=(getline line < current))>0) {print line > maximum; copied++}
            if(copyrc<0 || copied!=n) exit 4
            if(close(current)!=0 || close(maximum)!=0) exit 5
            best=total; bestpid=pid
        }
    }
    BEGIN {
        best=-1; bestpid="-"; count=0
        for(i=1;i<ARGC;i++) {
            path=ARGV[i]; pid=path; sub(/^.*\//,"",pid)
            if(pid !~ /^[1-9][0-9]*$/) continue
            count++; scan(path,pid)
        }
        if(count==0) exit 6
        printf "MAX_SMAPS_PID=%s\n",bestpid
        exit 0
    }' "$PROC_ROOT"/[0-9]* > "$BUFFER_DIR/summaries" || fail scan
    last=$(mread max_pid $BB tail -n1 "$BUFFER_DIR/summaries") || exit 1
    case "$last" in MAX_SMAPS_PID=*) MAXPID=${last#MAX_SMAPS_PID=} ;; *) fail max_pid ;; esac
    case "$MAXPID" in -) ;; ''|*[!0-9]*) fail max_pid ;; esac
    $BB cat "$BUFFER_DIR/summaries" || fail summaries_read
else
    printf 'MAX_SMAPS_PID=-\n'
fi
printf 'BEGIN_MAX_SMAPS\n'
if [ "$MAXPID" != '-' ]; then $BB cat "$BUFFER_DIR/max" || fail max_read; fi
printf 'END_MAX_SMAPS\n'
UP2=$(mread uptime_end $BB cut -d' ' -f1 "$PROC_ROOT/uptime") || exit 1
DURATION=$($BB awk -v a="$UP1" -v b="$UP2" 'BEGIN {
    if (a !~ /^[0-9]+([.][0-9]+)?$/ || b !~ /^[0-9]+([.][0-9]+)?$/ || b < a) exit 1
    printf "%.2f", b - a
}') || fail duration
cleanup || fail cleanup
printf 'UPTIME_END=%s\nSAMPLE_DURATION_SECONDS=%s\nDEVICE_RC=0\nEND_SAMPLE\n' "$UP2" "$DURATION"
