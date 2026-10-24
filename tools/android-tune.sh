#!/bin/bash
#
# Background wake-ups the ford container does not need on a 456 MiB player.
# Each entry is a component that brings an app's process up on its own: at
# BOOT_COMPLETED or on charging, battery, storage and network broadcasts.
# Firefox alone came up from RescheduleReceiver at boot with three processes,
# about 88 MB PSS, before anyone opened it (devlog 2026-10-19-android-opt-image.md).
#
#   tools/android-tune.sh apply     pm disable every component below
#   tools/android-tune.sh revert    pm enable them again
#   tools/android-tune.sh status    show the enabled state of each
#
# Runs against the live container through tools/android-boot.sh sh. The state is
# kept in the container's /data, so a fresh /data image needs apply again.
# A running WorkManager re-enables its receivers at once (F-Droid's
# RescheduleReceiver came back within a second), so apply force-stops each app
# first. Opening the app later can re-enable them; apply again after that.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOOT="$ROOT/tools/android-boot.sh"

PACKAGES=(org.mozilla.firefox org.fdroid.fdroid)
WORK=androidx.work.impl.background.systemalarm
COMPONENTS=(
    org.mozilla.firefox/$WORK.RescheduleReceiver
    org.mozilla.firefox/$WORK.ConstraintProxy\$BatteryChargingProxy
    org.mozilla.firefox/$WORK.ConstraintProxy\$BatteryNotLowProxy
    org.mozilla.firefox/$WORK.ConstraintProxy\$StorageNotLowProxy
    org.mozilla.firefox/$WORK.ConstraintProxy\$NetworkStateProxy
    org.mozilla.firefox/com.adjust.sdk.AdjustPreinstallReferrerReceiver
    # Before F-Droid's other entries: every pm disable sends the package a
    # PACKAGE_CHANGED, this receiver starts F-Droid for it (and logs "unsupported
    # action"), and the started WorkManager re-enables RescheduleReceiver. F-Droid
    # still rescans installed apps when it starts.
    org.fdroid.fdroid/.receiver.PackageManagerReceiver
    org.fdroid.fdroid/$WORK.RescheduleReceiver
    org.fdroid.fdroid/$WORK.ConstraintProxy\$BatteryChargingProxy
    org.fdroid.fdroid/$WORK.ConstraintProxy\$BatteryNotLowProxy
    org.fdroid.fdroid/$WORK.ConstraintProxy\$StorageNotLowProxy
    org.fdroid.fdroid/$WORK.ConstraintProxy\$NetworkStateProxy
    org.fdroid.fdroid/.receiver.StartupReceiver
)

csh()
{
    "$BOOT" sh "$1" 2>&1 | tr -d '\r' | { grep -v ' file pushed, ' || true; }
}

case "${1:-}" in
apply)
    for pkg in "${PACKAGES[@]}"; do csh "am force-stop $pkg"; done
    for c in "${COMPONENTS[@]}"; do csh "pm disable '$c'"; done
    for pkg in "${PACKAGES[@]}"; do csh "am force-stop $pkg"; done
    ;;
revert)
    for c in "${COMPONENTS[@]}"; do csh "pm enable '$c'"; done
    ;;
status)
    for pkg in "${PACKAGES[@]}"; do
        echo "== $pkg"
        csh "dumpsys package $pkg" \
            | awk '/disabledComponents:/ {p = 1} p && /^ *User [0-9]|grantedPermissions/ {exit} p' \
            | sed 's/^ *//'
    done
    ;;
*)
    echo "usage: tools/android-tune.sh apply | revert | status" >&2
    exit 2
    ;;
esac
