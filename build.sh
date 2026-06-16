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
