#!/bin/sh
# 一键验收：等待设备 -> 记录触摸注入旁证 -> 启动游戏模式
# 用法: ./verify-game.sh
DIR="$(dirname "$0")"
cd "$DIR" || exit 1

WAIT_SECS="${WAIT_SECS:-600}"
echo "== 等待设备（最多 ${WAIT_SECS} 秒，请插好 USB 并确认手机上的调试授权弹窗）=="
i=0
while [ $i -lt "$WAIT_SECS" ]; do
    if adb devices 2>/dev/null | awk 'NR==2 && $2=="device" {found=1} END {exit !found}'; then
        break
    fi
    i=$((i + 1))
    sleep 1
done
if ! adb devices 2>/dev/null | awk 'NR==2 && $2=="device" {found=1} END {exit !found}'; then
    echo "未检测到设备：请检查 USB 线 / 手机上的 USB 调试授权。"
    exit 1
fi
echo "== 设备在线: $(adb devices | awk 'NR==2 {print $1}') =="

# 后台记录注入触摸（验收后把文件名发我即可）
adb shell getevent -l > /tmp/game_getevent.log 2>&1 &
GETEVENT_PID=$!
trap 'kill $GETEVENT_PID 2>/dev/null' EXIT

echo "== 启动游戏模式 =="
./run-game.sh "$@"
status=$?

echo "== 会话结束 (exit=$status)，触摸注入旁证: /tmp/game_getevent.log =="
echo "把上面的 exit 码和现象告诉我即可。"
