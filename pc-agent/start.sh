#!/usr/bin/env bash
# Xiaomiao Agent 一键启动脚本（goal 节点 11）。
# 用法: ./start.sh [--host 0.0.0.0] [--port 8766]
# 首次运行或 .venv 缺失时自动创建虚拟环境并安装 requirements.txt。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SYSTEM_TYPE="$(uname -s)"
case "$SYSTEM_TYPE" in
    Darwin*)
        HOST_PYTHON="python3"
        VENV_DIR=".venv/macos"
        VENV_PYTHON="$VENV_DIR/bin/python"
        ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT*)
        HOST_PYTHON="python"
        VENV_DIR=".venv/windows"
        VENV_PYTHON="$VENV_DIR/Scripts/python.exe"
        ;;
    Linux*)
        HOST_PYTHON="python3"
        VENV_DIR=".venv/linux"
        VENV_PYTHON="$VENV_DIR/bin/python"
        ;;
    *)
        echo "start.sh: unsupported system: $SYSTEM_TYPE" >&2
        exit 1
        ;;
esac

if ! command -v "$HOST_PYTHON" >/dev/null 2>&1; then
    echo "start.sh: $HOST_PYTHON was not found for $SYSTEM_TYPE" >&2
    exit 1
fi

PY="$VENV_PYTHON"
if [ ! -x "$PY" ]; then
    echo "start.sh: creating virtualenv at $VENV_DIR ..."
    "$HOST_PYTHON" -m venv "$VENV_DIR"
fi

if [ ! -x "$PY" ]; then
    echo "start.sh: virtualenv did not create $PY" >&2
    exit 1
fi

if ! "$PY" -c "import psutil" 2>/dev/null; then
    echo "start.sh: installing dependencies ..."
    "$PY" -m pip install -r requirements.txt
fi

exec "$PY" monitor.py "$@"
