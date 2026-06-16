#!/usr/bin/env bash
set -e

# Build script for IAmPad-Zygisk
# Usage: ./build.sh [NDK_PATH]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "$1" ]; then
    if [ -n "$ANDROID_HOME" ]; then
        NDK_ROOT="$(find "$ANDROID_HOME/ndk" -maxdepth 1 -type d | sort | tail -n 1)"
    fi
else
    NDK_ROOT="$1"
fi

if [ -z "$NDK_ROOT" ] || [ ! -d "$NDK_ROOT" ]; then
    echo "Error: Android NDK not found."
    echo "Please set ANDROID_HOME or pass NDK path as argument."
    exit 1
fi

echo "Using NDK: $NDK_ROOT"

# Build with ndk-build
"$NDK_ROOT/ndk-build" -C "$SCRIPT_DIR/module/jni" \
    NDK_PROJECT_PATH="$SCRIPT_DIR/module" \
    APP_BUILD_SCRIPT="$SCRIPT_DIR/module/jni/Android.mk" \
    NDK_APPLICATION_MK="$SCRIPT_DIR/module/jni/Application.mk" \
    clean

"$NDK_ROOT/ndk-build" -C "$SCRIPT_DIR/module/jni" \
    NDK_PROJECT_PATH="$SCRIPT_DIR/module" \
    APP_BUILD_SCRIPT="$SCRIPT_DIR/module/jni/Android.mk" \
    NDK_APPLICATION_MK="$SCRIPT_DIR/module/jni/Application.mk"

# Package module
OUTPUT_DIR="$SCRIPT_DIR/out"
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/zygisk"

cp "$SCRIPT_DIR/module.prop" "$OUTPUT_DIR/"
cp "$SCRIPT_DIR/customize.sh" "$OUTPUT_DIR/"
cp "$SCRIPT_DIR/config.conf" "$OUTPUT_DIR/"

# Magisk Zygisk naming convention:
# arm64-v8a  -> zygisk-<id>.so
# armeabi-v7a -> zygisk-<id>.so.arm
# x86        -> zygisk-<id>.so.x86
# x86_64     -> zygisk-<id>.so.x86_64

MODULE_ID="iampad"
SO_NAME="zygisk-${MODULE_ID}.so"

cp "$SCRIPT_DIR/module/libs/arm64-v8a/libiampad.so"   "$OUTPUT_DIR/zygisk/$SO_NAME"
cp "$SCRIPT_DIR/module/libs/armeabi-v7a/libiampad.so" "$OUTPUT_DIR/zygisk/${SO_NAME}.arm"
cp "$SCRIPT_DIR/module/libs/x86/libiampad.so"         "$OUTPUT_DIR/zygisk/${SO_NAME}.x86"
cp "$SCRIPT_DIR/module/libs/x86_64/libiampad.so"      "$OUTPUT_DIR/zygisk/${SO_NAME}.x86_64"

cd "$OUTPUT_DIR"
zip -r9 "$SCRIPT_DIR/IAmPad-Zygisk.zip" .

echo "Built: $SCRIPT_DIR/IAmPad-Zygisk.zip"
