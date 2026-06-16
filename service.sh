#!/system/bin/sh
# IAmPad-Zygisk service script - runs on boot
# Creates log directory and writes initial diagnostics

MODDIR=${0%/*}
LOGFILE="$MODDIR/iampad.log"

# Clear old log
echo "" > "$LOGFILE"
echo "============================================" >> "$LOGFILE"
echo "IAmPad-Zygisk service.sh - $(date)" >> "$LOGFILE"
echo "============================================" >> "$LOGFILE"

# Check if Zygisk is available
if [ -d "/data/adb/modules/zygisksu" ] || [ -d "/data/adb/modules/zygisk_assistant" ]; then
    echo "[OK] ZygiskNext found" >> "$LOGFILE"
else
    echo "[WARN] ZygiskNext NOT found! Zygisk modules will not work!" >> "$LOGFILE"
    echo "[WARN] Install from: https://github.com/Dr-TSNG/ZygiskNext/releases" >> "$LOGFILE"
fi

# Check module files
echo "" >> "$LOGFILE"
echo "Module files:" >> "$LOGFILE"
ls -la "$MODDIR/" >> "$LOGFILE" 2>&1
echo "" >> "$LOGFILE"
echo "Zygisk SO files:" >> "$LOGFILE"
ls -la "$MODDIR/zygisk/" >> "$LOGFILE" 2>&1

# Check config
echo "" >> "$LOGFILE"
echo "Config:" >> "$LOGFILE"
cat "$MODDIR/config.conf" >> "$LOGFILE" 2>&1

echo "" >> "$LOGFILE"
echo "service.sh done. Waiting for module logs..." >> "$LOGFILE"
