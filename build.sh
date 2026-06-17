#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "$1" ]; then
    echo "Usage: ./build.sh <NDK_PATH>"
    echo "Example: ./build.sh \$ANDROID_HOME/ndk/26.3.11579264"
    exit 1
fi

NDK_ROOT="$1"

if [ ! -d "$NDK_ROOT" ]; then
    echo "Error: NDK directory not found: $NDK_ROOT"
    exit 1
fi

NDK_BUILD="$NDK_ROOT/ndk-build"
if [ ! -f "$NDK_BUILD" ]; then
    echo "Error: ndk-build not found at: $NDK_BUILD"
    exit 1
fi

echo "Using NDK: $NDK_ROOT"
echo "ndk-build: $NDK_BUILD"

JNI_DIR="$SCRIPT_DIR/module/jni"

# Clean
"$NDK_BUILD" -C "$JNI_DIR" clean 2>/dev/null || true

# Build all ABIs
"$NDK_BUILD" -C "$JNI_DIR" \
    NDK_PROJECT_PATH="$SCRIPT_DIR" \
    APP_BUILD_SCRIPT="$JNI_DIR/Android.mk" \
    NDK_APPLICATION_MK="$JNI_DIR/Application.mk" \
    NDK_LIBS_OUT="$SCRIPT_DIR/out/lib" \
    NDK_OUT="$SCRIPT_DIR/out/obj"

echo ""
echo "Build complete! Libraries:"
find "$SCRIPT_DIR/out/lib" -name "*.so" 2>/dev/null || echo "No .so files found"

PKG_DIR="$SCRIPT_DIR/out/module"
ZIP_PATH="$SCRIPT_DIR/IAmPad-Zygisk.zip"

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/zygisk"
cp -f "$SCRIPT_DIR/module.prop" "$PKG_DIR/"
cp -f "$SCRIPT_DIR/customize.sh" "$PKG_DIR/"
cp -f "$SCRIPT_DIR/config.conf" "$PKG_DIR/"
cp -f "$SCRIPT_DIR/out/lib/arm64-v8a/libiampad.so" "$PKG_DIR/zygisk/arm64-v8a.so"
cp -f "$SCRIPT_DIR/out/lib/armeabi-v7a/libiampad.so" "$PKG_DIR/zygisk/armeabi-v7a.so"

(
    cd "$PKG_DIR"
    zip -r9 "$ZIP_PATH" .
)

echo ""
echo "Package complete: $ZIP_PATH"
