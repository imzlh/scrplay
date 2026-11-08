#!/bin/sh
# 启动 scrplay 游戏模式（开发构建）
# 用法: ./run-game.sh [额外的 scrplay 参数...]
# 例:  ./run-game.sh --game-profile=default --max-size=1600
DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export SCRPLAY_SERVER_PATH="$DIR/server/scrplay-server"
export SCRPLAY_ICON_PATH="$DIR/app/data/icon.png"

# SDL2 在 X11/XWayland 下要连输入法必须显式指定 IME 模块；
# 不设置的话 fcitx5 根本接不上窗口，编辑器里无法输入中文。
# 只在用户环境没有时才补默认值（fcitx5 优先，其次 ibus）。
if [ -z "$SDL_IM_MODULE" ] && [ -z "$XMODIFIERS" ]; then
    if pgrep -x fcitx5 >/dev/null 2>&1; then
        export SDL_IM_MODULE=fcitx
        export XMODIFIERS=@im=fcitx
    elif pgrep -x ibus-daemon >/dev/null 2>&1; then
        export SDL_IM_MODULE=ibus
        export XMODIFIERS=@im=ibus
    fi
fi

exec "$DIR/build/app/scrplay" --game-mode "$@"
