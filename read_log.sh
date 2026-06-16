#!/system/bin/sh
# IAmPad-Zygisk - Read logs from phone
# Run this in any terminal app (Termux, etc.)
# Usage: sh /data/adb/modules/iampad/read_log.sh

LOGFILE="/data/adb/modules/iampad/iampad.log"

if [ ! -f "$LOGFILE" ]; then
    echo "Log file not found: $LOGFILE"
    echo "Module may not be installed or phone hasn't been rebooted yet."
    exit 1
fi

echo "=========================================="
echo "  IAmPad-Zygisk Log Viewer"
echo "=========================================="
echo ""
echo "1) View full log"
echo "2) View last 50 lines"
echo "3) View errors only"
echo "4) View hook status"
echo "5) Tail live log (Ctrl+C to stop)"
echo ""
echo -n "Choose [1-5]: "
read choice

case "$choice" in
    1) cat "$LOGFILE" ;;
    2) tail -50 "$LOGFILE" ;;
    3) grep -i "error\|fail\|warn" "$LOGFILE" ;;
    4) grep -i "hook\|pltHook\|FOUND\|VERIFY\|spoof" "$LOGFILE" ;;
    5) tail -f "$LOGFILE" ;;
    *) echo "Invalid choice" ;;
esac
