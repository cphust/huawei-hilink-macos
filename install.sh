#!/bin/sh
# 当前用户安装：只注册 USB 事件任务，不调整网络服务顺序。
set -eu
umask 077
cd "$(dirname "$0")"
[ "$#" -eq 0 ] || { echo 'usage: sh install.sh' >&2; exit 2; }
[ "$(id -u)" -ne 0 ] || { echo '请以当前登录用户运行，不要使用 sudo。' >&2; exit 1; }

LABEL=local.hilink-switch
DEST="$HOME/Library/Application Support/HiLinkSwitch"
BIN="$DEST/hilink-switch"
MARKER="$DEST/.managed-by-hilink-switch"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/hilink-switch.log"
DOMAIN="gui/$(id -u)"
OWNER_TAG='HiLinkSwitch E8372h-155 managed installation v1'

if [ -e "$BIN" ] || [ -e "$PLIST" ]; then
    [ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "$OWNER_TAG" ] ||
        { echo '发现其他安装留下的同名文件，未覆盖。请先核对。' >&2; exit 1; }
fi
sh build.sh
./build/hilink-switch --status > build/install-status.txt
# 先证明当前 USB 能承载请求，再写入任何后台配置；不关闭 Wi-Fi 或 VPN。
/usr/bin/python3 check.py > build/install-usb-check.jsonl

mkdir -p "$DEST" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
chmod 700 "$DEST"
install -m 700 build/hilink-switch "$BIN"
cp local.hilink-switch.plist.in "$PLIST"
chmod 600 "$PLIST"
plutil -insert ProgramArguments.0 -string "$BIN" "$PLIST"
plutil -insert StandardOutPath -string "$LOG" "$PLIST"
plutil -insert StandardErrorPath -string "$LOG" "$PLIST"
plutil -lint "$PLIST" > /dev/null
touch "$LOG"
chmod 600 "$LOG"
printf '%s\n' "$OWNER_TAG" > "$MARKER"

if launchctl print "$DOMAIN/$LABEL" > /dev/null 2>&1; then
    launchctl bootout "$DOMAIN/$LABEL"
fi
if ! launchctl bootstrap "$DOMAIN" "$PLIST"; then
    rm -f "$PLIST"
    echo '后台任务未加载；已移除启动配置，程序和日志保留供排查。' >&2
    exit 1
fi
launchctl print "$DOMAIN/$LABEL" > /dev/null
echo '当前用户的插入/登录自动切换已启用；网络优先级未修改。'
echo "日志：$LOG"
