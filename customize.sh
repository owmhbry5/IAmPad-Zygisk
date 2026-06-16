#!/system/bin/sh
# IAmPad-Zygisk installation script

ui_print "============================================"
ui_print "  IAmPad-Zygisk - Open Source Tablet Module"
ui_print "============================================"
ui_print ""
ui_print "Target: WeChat, QQ, TIM, DingTalk"
ui_print ""

# Make scripts executable
chmod 755 "$MODDIR/service.sh"
chmod 755 "$MODDIR/read_log.sh"

ui_print "After reboot, check logs with:"
ui_print "  sh /data/adb/modules/iampad/read_log.sh"
ui_print ""
ui_print "Or in Termux:"
ui_print "  cat /data/adb/modules/iampad/iampad.log"
ui_print ""
ui_print "Reboot to activate."
