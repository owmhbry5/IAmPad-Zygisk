#!/system/bin/sh

PKG="com.tencent.mm"
DATA_DIR="/data/data/$PKG"

echo "IAmPad-Zygisk: restarting WeChat and clearing non-chat caches..."

am force-stop "$PKG" >/dev/null 2>&1 || true

if [ -d "$DATA_DIR" ]; then
  rm -rf "$DATA_DIR/cache" \
         "$DATA_DIR/code_cache" \
         "$DATA_DIR/app_webview/Default/Cache" \
         "$DATA_DIR/app_webview/Default/Code Cache" \
         "$DATA_DIR/app_webview/Default/GPUCache" \
         "$DATA_DIR/app_tbs" \
         "$DATA_DIR/files/tbs" \
         "$DATA_DIR/files/xlog/PUSH" 2>/dev/null || true
  echo "Cache cleanup finished. Chat data was not touched."
else
  echo "WeChat data directory not found: $DATA_DIR"
fi

am force-stop "$PKG" >/dev/null 2>&1 || true
echo "Done. Open WeChat again after a few seconds."
