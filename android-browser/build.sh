#!/usr/bin/env bash
# Build the WebView browser: android-browser/build.sh [OUT.apk]
#
# No Gradle: icon.py draws the launcher icon, aapt2 compiles the resources and writes R,
# then javac against the platform jar, d8, zipalign and apksigner. Signed with the SDK
# debug key, as WalkmanUI is.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-$HOME/Android/sdk}"
BT="$SDK/build-tools/35.0.0"
JAR="$SDK/platforms/android-28/android.jar"
JDK="${JAVA_HOME:-$ROOT/work/jdk11}"
OUT="${1:-$ROOT/work/browser/Browser.apk}"
B="$(mktemp -d)"
trap 'rm -r "$B"' EXIT

mkdir -p "$B/res" "$B/gen" "$B/classes" "$(dirname "$OUT")"
cp -r "$HERE/res/." "$B/res/"
python3 -I "$HERE/icon.py" "$B/res"
"$BT/aapt2" compile --dir "$B/res" -o "$B/res.zip"
"$BT/aapt2" link -I "$JAR" --manifest "$HERE/AndroidManifest.xml" --java "$B/gen" \
	-o "$B/base.apk" "$B/res.zip"
"$JDK/bin/javac" --release 8 -Xlint:-options -classpath "$JAR" -d "$B/classes" \
	$(find "$HERE/src" "$B/gen" -name '*.java')
"$BT/d8" --min-api 21 --lib "$JAR" --output "$B" $(find "$B/classes" -name '*.class')
# A fixed date, so the same sources give the same APK.
TZ=UTC touch -t 198101010000 "$B/classes.dex"
(cd "$B" && TZ=UTC zip -q -X base.apk classes.dex)
"$BT/zipalign" -p -f 4 "$B/base.apk" "$B/aligned.apk"
"$BT/apksigner" sign --ks "$HOME/.android/debug.keystore" --ks-pass pass:android \
	--out "$OUT" "$B/aligned.apk" 2>/dev/null
echo "$OUT: $(stat -c %s "$OUT") bytes"
