#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
OUT_DIR="$ROOT_DIR/module/helper"

mkdir -p "$BUILD_DIR"
mkdir -p "$OUT_DIR/libs/arm64-v8a"
mkdir -p "$OUT_DIR/libs/armeabi-v7a"
mkdir -p "$OUT_DIR/libs/x86"
mkdir -p "$OUT_DIR/libs/x86_64"

cd "$BUILD_DIR"

PINE_VERSION="0.2.7"
DEXKIT_VERSION="2.0.3"
FLATBUFFERS_VERSION="23.5.26"
KOTLIN_VERSION="1.9.24"
R8_VERSION="8.3.37"

download() {
    local url="$1"
    local file="$2"
    if [[ ! -f "$file" ]]; then
        echo "Downloading $file"
        curl -fL -o "$file" "$url"
    fi
}

if [[ -n "${ANDROID_HOME:-}" && -f "$ANDROID_HOME/platforms/android-34/android.jar" ]]; then
    ANDROID_JAR="$ANDROID_HOME/platforms/android-34/android.jar"
else
    download "https://github.com/Sable/android-platforms/raw/master/android-34/android.jar" "android-34.jar"
    ANDROID_JAR="$BUILD_DIR/android-34.jar"
fi

download "https://repo1.maven.org/maven2/top/canyie/pine/core/$PINE_VERSION/core-$PINE_VERSION.aar" "pine-core-$PINE_VERSION.aar"
download "https://repo1.maven.org/maven2/org/luckypray/dexkit/$DEXKIT_VERSION/dexkit-$DEXKIT_VERSION.aar" "dexkit-$DEXKIT_VERSION.aar"
download "https://repo1.maven.org/maven2/com/google/flatbuffers/flatbuffers-java/$FLATBUFFERS_VERSION/flatbuffers-java-$FLATBUFFERS_VERSION.jar" "flatbuffers-java-$FLATBUFFERS_VERSION.jar"
download "https://repo1.maven.org/maven2/org/jetbrains/kotlin/kotlin-stdlib/$KOTLIN_VERSION/kotlin-stdlib-$KOTLIN_VERSION.jar" "kotlin-stdlib-$KOTLIN_VERSION.jar"
download "https://storage.googleapis.com/r8-releases/raw/$R8_VERSION/r8lib.jar" "r8lib.jar"

extract_aar_libs() {
    local aar="$1"
    local name="$2"
    rm -rf "$name"
    unzip -q "$aar" -d "$name"
    cp "$name/jni/arm64-v8a/"*.so "$OUT_DIR/libs/arm64-v8a/"
    cp "$name/jni/armeabi-v7a/"*.so "$OUT_DIR/libs/armeabi-v7a/"
    if [[ -d "$name/jni/x86" ]]; then
        cp "$name/jni/x86/"*.so "$OUT_DIR/libs/x86/"
    fi
    if [[ -d "$name/jni/x86_64" ]]; then
        cp "$name/jni/x86_64/"*.so "$OUT_DIR/libs/x86_64/"
    fi
}

extract_aar_libs "pine-core-$PINE_VERSION.aar" "pine_prefab"
extract_aar_libs "dexkit-$DEXKIT_VERSION.aar" "dexkit_prefab"

rm -rf classes_pine classes_dexkit classes_flatbuffers classes_kotlin
unzip -q -o pine_prefab/classes.jar -d classes_pine
unzip -q -o dexkit_prefab/classes.jar -d classes_dexkit
unzip -q -o flatbuffers-java-$FLATBUFFERS_VERSION.jar -d classes_flatbuffers
unzip -q -o kotlin-stdlib-$KOTLIN_VERSION.jar -d classes_kotlin

rm -rf classes_helper
mkdir -p classes_helper
CP="classes_pine:classes_dexkit:classes_flatbuffers:classes_kotlin:$ANDROID_JAR"

javac -source 8 -target 8 -cp "$CP" -d classes_helper \
    "$SCRIPT_DIR/src/main/java/com/iampad/helper/"*.java

java -cp "r8lib.jar" com.android.tools.r8.D8 \
    --release \
    --min-api 24 \
    --lib "$ANDROID_JAR" \
    --output "$BUILD_DIR/helper.dex.zip" \
    classes_helper classes_pine classes_dexkit classes_flatbuffers classes_kotlin

unzip -q -o "$BUILD_DIR/helper.dex.zip" -d "$OUT_DIR"
mv "$OUT_DIR/classes.dex" "$OUT_DIR/helper.dex"

echo "Built helper.dex and native libs in $OUT_DIR"
find "$OUT_DIR" -type f
