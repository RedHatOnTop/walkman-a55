# Sourced by the device tools: the adb serial of the Walkman they drive.
#
#   . tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
#
# NW_SERIAL, else the first line of work/serial (gitignored, so the serial
# stays out of the repo). With neither the tools stop: other Android devices
# are often attached, and `adb -s ""` would talk to whichever adb picks.
nw_serial()
{
    local s="${NW_SERIAL:-}"
    [ -n "$s" ] || s="$(sed -n '1{s/[[:space:]]//g;p}' "$(dirname "${BASH_SOURCE[0]}")/../work/serial" 2>/dev/null)"
    if [ -z "$s" ]; then
        echo "no Walkman serial: set NW_SERIAL or put it in work/serial (adb devices lists it)" >&2
        return 1
    fi
    echo "$s"
}
