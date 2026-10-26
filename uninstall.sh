#!/bin/sh
# 只移除本项目拥有的程序和启动配置；保留日志与系统自动创建的网络服务。
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh uninstall.sh' >&2; exit 2; }
[ "$(id -u)" -ne 0 ] || { echo '请以安装时的普通用户运行，不要使用 sudo。' >&2; exit 1; }
LABEL=local.hilink-switch
DEST="$HOME/Library/Application Support/HiLinkSwitch"
MARKER="$DEST/.managed-by-hilink-switch"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"
OWNER_TAG='HiLinkSwitch E8372h-155 managed installation v1'

if [ ! -e "$MARKER" ] && [ ! -e "$PLIST" ] && [ ! -e "$DEST/hilink-switch" ]; then
    echo '本项目尚未安装，无需卸载。'
    exit 0
fi
[ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "$OWNER_TAG" ] ||
    { echo '安装归属不明，未删除任何文件。' >&2; exit 1; }
if launchctl print "$DOMAIN/$LABEL" > /dev/null 2>&1; then
    launchctl bootout "$DOMAIN/$LABEL"
fi
rm -f "$PLIST" "$DEST/hilink-switch" "$MARKER"
rmdir "$DEST" 2>/dev/null || true
echo '自动任务与本项目程序已移除；日志、项目源码及现有网络配置保留。'
echo '已处于 USB 网卡模式的设备可以继续使用；拔插后的自动切换不再由本工具执行。'
