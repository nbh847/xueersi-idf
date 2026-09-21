@echo off
rem Xiaomiao Agent 一键启动脚本（goal 节点 11）。
rem 用法: pc-agent\start.bat [--host 0.0.0.0] [--port 8766]
rem 首次运行或 .venv 缺失时自动创建虚拟环境并安装 requirements.txt。

setlocal
cd /d "%~dp0"

set "PY=.venv\Scripts\python.exe"
if not exist "%PY%" (
    echo start.bat: creating virtualenv at .venv ...
    python -m venv .venv
    if errorlevel 1 (
        echo start.bat: failed to create virtualenv
        exit /b 1
    )
)

"%PY%" -c "import psutil" >nul 2>nul
if errorlevel 1 (
    echo start.bat: installing dependencies ...
    "%PY%" -m pip install -r requirements.txt
    if errorlevel 1 (
        echo start.bat: failed to install dependencies
        exit /b 1
    )
)

"%PY%" monitor.py %*
endlocal