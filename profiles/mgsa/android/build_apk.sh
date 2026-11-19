#!/usr/bin/env bash
# Package the mgsa NativeActivity as an APK without Gradle (uses SDK tools).
#
# Prerequisites:
#   - Android SDK (ANDROID_HOME) with build-tools and a platform installed.
#   - libmgsa.so built for arm64-v8a (see CMakeLists.txt / README.md).
#
# Usage:
#   build_apk.sh <path-to-libmgsa.so> <output.apk> [game-root-dir]
#
# The game data (extract/PSP_GAME) must be installed separately on the device
# under the app's files dir; pass it as game-root-dir to include it in the APK
# assets (the app unpacks it on first run).
set -euo pipefail

so="$1"
output="$2"
game_root="${3:-}"

here="$(cd "$(dirname "$0")" && pwd)"
sdk="${ANDROID_HOME:?set ANDROID_HOME to the Android SDK}"
build_tools="$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)"
platform="$(ls -d "$sdk"/platforms/android-* | sort -V | tail -1)"
android_jar="$platform/android.jar"

work="$here/build/apk"
rm -rf "$work"
mkdir -p "$work/classes" "$work/dex" "$work/lib/arm64-v8a" "$work/assets" "$work/res"

echo "compiling NativeActivity Java"
javac -nowarn --release 11 -classpath "$android_jar" -d "$work/classes" \
    "$here/java/io/github/juanklv2003/mgsa/MgsaActivity.java"

echo "dexing"
class_files=()
while IFS= read -r -d '' f; do class_files+=("$f"); done < <(find "$work/classes" -name '*.class' -print0)
"$build_tools/d8" --release --min-api 29 --lib "$android_jar" --output "$work/dex" "${class_files[@]}"

echo "staging native library and resources"
cp "$so" "$work/lib/arm64-v8a/libmgsa.so"
cp -r "$here"/res/* "$work/res/" 2>/dev/null || true
if [[ -n "$game_root" && -d "$game_root" ]]; then
    echo "staging game data"
    mkdir -p "$work/assets/extract"
    cp -r "$game_root/PSP_GAME" "$work/assets/extract/"
fi

echo "building unsigned APK"
(
    cd "$work"
    # aapt2.exe is a Win32 binary: it needs backslash paths on Windows hosts.
    work_win="$(cygpath -w "$work" 2>/dev/null || echo "$work")"
    res_str="$work_win\\res"
    flata="$work_win\\compiled.flata"
    appres="$work_win\\app-res.apk"
    java_win="$work_win\\classes"
    aapt2 compile --dir "$res_str" -o "$flata"
    aapt2 link -o "$appres" -I "$android_jar" --manifest "$here/AndroidManifest.xml" \
        --java "$java_win" --min-sdk-version 29 --target-sdk-version 34 \
        "$flata"
    javac -nowarn --release 11 -classpath "$android_jar" -d classes \
        classes/io/github/juanklv2003/mgsa/R.java 2>/dev/null || true
    # Re-dex including R
    rm -rf dex && mkdir dex
    mapfile -d '' cf < <(find classes -name '*.class' -print0)
    "$build_tools/d8" --release --min-api 29 --lib "$android_jar" --output dex "${cf[@]}"
    cp app-res.apk "$work/final.apk"
    cd "$work"
    zip -q -r "$output" final.apk classes.dex lib assets 2>/dev/null || \
        "$build_tools/aapt" add "$output" lib/arm64-v8a/libmgsa.so
)

echo "APK at $output (unsigned; sign with apksigner for install)"
