#!/bin/sh
# 摇杆问题取证：等待设备 -> 打开显示触摸 -> 启动自测注入 -> 连拍截屏 -> 拉取日志
# 用法: ./selftest.sh
DIR="$(dirname "$0")"
cd "$DIR" || exit 1

WAIT_SECS="${WAIT_SECS:-600}"
echo "== 等待设备（最多 ${WAIT_SECS} 秒）=="
i=0
while [ $i -lt "$WAIT_SECS" ]; do
    if adb devices 2>/dev/null | awk 'NR==2 && $2=="device" {found=1} END {exit !found}'; then
        break
    fi
    i=$((i + 1))
    sleep 1
done
if ! adb devices 2>/dev/null | awk 'NR==2 && $2=="device" {found=1} END {exit !found}'; then
    echo "设备未上线，退出。"
    exit 1
fi
SERIAL=$(adb devices | awk 'NR==2 {print $1}')
echo "== 设备: $SERIAL =="

echo "== 屏幕信息 =="
adb shell wm size
adb shell dumpsys input 2>/dev/null | grep -i "SurfaceOrientation" | head -1

adb shell settings put system show_touches 1
adb logcat -c 2>/dev/null

echo "== 启动自测注入（SCRPLAY_GAME_SELFTEST=1）=="
SCRPLAY_SERVER_PATH="$DIR/server/scrplay-server" \
SCRPLAY_GAME_SELFTEST=1 \
    "$DIR/build/app/scrplay" --game-mode --window-title=SELFTEST \
    > /tmp/selftest_client.log 2>&1 &
SCRCPPID=$!

echo "== 连拍截屏（注入时间线约在启动后 3-8 秒）=="
i=0
while [ $i -lt 14 ]; do
    adb exec-out screencap -p > /tmp/selftest_shot_$i.png 2>/dev/null
    sleep 0.7
    i=$((i + 1))
done

wait $SCRCPPID 2>/dev/null
adb shell settings put system show_touches 0
# 预编译 server jar 是上游原封件，logcat tag 仍是 "scrcpy"（Ln.java TAG），
# 过滤器必须跟随 jar 的实际 tag，不能跟产品名走
adb logcat -d -s scrcpy:V > /tmp/selftest_logcat.txt 2>&1
echo "== 完成 =="
echo "客户端日志: /tmp/selftest_client.log（找 SELFTEST: 行）"
echo "截屏: /tmp/selftest_shot_0..13.png（找触摸圆点落点）"
echo "服务端日志: /tmp/selftest_logcat.txt"
