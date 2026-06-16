#!/system/bin/sh
# IAmPad-Zygisk service.sh

MODDIR=${0%/*}
LOGFILE="/data/local/tmp/iampad.log"

echo "" > "$LOGFILE"
echo "==========================================" >> "$LOGFILE"
echo "IAmPad service.sh - $(date)" >> "$LOGFILE"
echo "==========================================" >> "$LOGFILE"

# Check ZygiskNext
if [ -d "/data/adb/modules/zygisksu" ]; then
    echo "[OK] ZygiskNext (zygisksu) found" >> "$LOGFILE"
elif [ -d "/data/adb/modules/zygisk_assistant" ]; then
    echo "[OK] ZygiskNext (zygisk_assistant) found" >> "$LOGFILE"
else
    echo "[FAIL] ZygiskNext NOT found!" >> "$LOGFILE"
    echo "Install: https://github.com/Dr-TSNG/ZygiskNext/releases" >> "$LOGFILE"
fi

# Check if module is disabled
if [ -f "$MODDIR/disable" ]; then
    echo "[FAIL] Module is DISABLED!" >> "$LOGFILE"
fi

# Check .so files
echo "" >> "$LOGFILE"
echo "SO files:" >> "$LOGFILE"
ls -la "$MODDIR/zygisk/" >> "$LOGFILE" 2>&1

# Check Zygisk marker
echo "" >> "$LOGFILE"
echo "Checking native module marker..." >> "$LOGFILE"
sleep 10  # Wait for module to potentially load
if [ -f "/data/local/tmp/iampad_loaded.marker" ]; then
    echo "[OK] Native module marker found!" >> "$LOGFILE"
    cat "/data/local/tmp/iampad_loaded.marker" >> "$LOGFILE"
else
    echo "[FAIL] Native module marker NOT found" >> "$LOGFILE"
    echo "This means the .so file was never loaded by ZygiskNext" >> "$LOGFILE"
    echo "Possible causes:" >> "$LOGFILE"
    echo "  1. ZygiskNext not configured to load modules" >> "$LOGFILE"
    echo "  2. Module disabled in KernelSU" >> "$LOGFILE"
    echo "  3. Zygisk API version mismatch" >> "$LOGFILE"
fi

echo "" >> "$LOGFILE"
echo "service.sh done." >> "$LOGFILE"
