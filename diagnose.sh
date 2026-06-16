#!/system/bin/sh
# IAmPad-Zygisk diagnostics
LOG="/data/local/tmp/iampad_diag.log"
echo "=== IAmPad Diagnostics $(date) ===" > "$LOG"

echo "" >> "$LOG"
echo "1. KernelSU module status:" >> "$LOG"
ls -la /data/adb/modules/iampad/ >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "2. Check if module is disabled:" >> "$LOG"
if [ -f /data/adb/modules/iampad/disable ]; then
    echo "   ❌ MODULE IS DISABLED!" >> "$LOG"
else
    echo "   ✅ Module is enabled" >> "$LOG"
fi
echo "" >> "$LOG"

echo "3. ZygiskNext modules:" >> "$LOG"
ls -la /data/adb/modules/zygisksu/ >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "4. Check ZygiskNext config:" >> "$LOG"
if [ -f /data/adb/zygisksu/config ]; then
    cat /data/adb/zygisksu/config >> "$LOG" 2>&1
else
    echo "   No ZygiskNext config found" >> "$LOG"
fi
echo "" >> "$LOG"

echo "5. Check module.prop:" >> "$LOG"
cat /data/adb/modules/iampad/module.prop >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "6. Check zygisk directory permissions:" >> "$LOG"
ls -la /data/adb/modules/iampad/zygisk/ >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "7. Magisk/KernelSU version:" >> "$LOG"
magisk -c >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "8. Android version:" >> "$LOG"
getprop ro.build.version.release >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "9. Zygisk enabled?" >> "$LOG"
getprop ro.zygisk >> "$LOG" 2>&1
echo "" >> "$LOG"

echo "=== Done ===" >> "$LOG"
echo "Diagnostics saved to $LOG"
