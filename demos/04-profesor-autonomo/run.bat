@echo off
rem Professor Stack-chan, standalone: build and flash the robot. After this the robot
rem talks to Gemini by itself over Wi-Fi: no laptop needed.
rem The Gemini key is taken from GEMINI_API_KEY (setx GEMINI_API_KEY "your-key") and
rem the Wi-Fi from config\wifi_secrets.h. Connect the robot with a USB-C data cable.
set ROOT=%~dp0..\..
if not exist "%ROOT%\.venv\Scripts\pio.exe" (echo Missing .venv: run setup.bat first & exit /b 1)
if not exist "%ROOT%\config\wifi_secrets.h" (echo Missing config\wifi_secrets.h: copy config\wifi_secrets.h.example and fill it in & exit /b 1)
"%ROOT%\.venv\Scripts\pio.exe" run -d "%~dp0firmware" -t upload
