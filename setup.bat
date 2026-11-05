@echo off
rem Creates .venv with PlatformIO and the Python packages for the relay.
cd /d %~dp0
python -m venv .venv || exit /b 1
.venv\Scripts\python.exe -m pip install -r requirements.txt
if not exist config\wifi_secrets.h copy config\wifi_secrets.h.example config\wifi_secrets.h && echo Edit config\wifi_secrets.h with your Wi-Fi
