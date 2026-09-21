#!/usr/bin/env bash
# Xiaomiao Agent 一键启动脚本（goal 节点 11）。
# 用法: ./start.sh [--host 0.0.0.0] [--port 8766]
# 首次运行或 .venv 缺失时自动创建虚拟环境并安装 requirements.txt。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR=".venv"
if [ -x "$VENV_DIR/Scripts/python.exe" ]; then
    PY="$VENV_DIR/Scripts/python.exe"
elif [ -x "$VENV_DIR/bin/python" ]; then
    PY="$VENV_DIR/bin/python"
else
    echo "start.sh: creating virtualenv at $VENV_DIR ..."
    python -m venv "$VENV_DIR"
    if [ -x "$VENV_DIR/Scripts/python.exe" ]; then
        PY="$VENV_DIR/Scripts/python.exe"
    else
        PY="$VENV_DIR/bin/python"
    fi
fi

if ! "$PY" -c "import psutil" 2>/dev/null; then
    echo "start.sh: installing dependencies ..."
    "$PY" -m pip install -r requirements.txt
fi

exec "$PY" monitor.py "$@"