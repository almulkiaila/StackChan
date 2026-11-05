@echo off
rem Professor Stack-chan: build + flash the robot, then start the Gemini relay.
rem   run.bat            flash and start
rem   run.bat -NoFlash   only start the relay (the robot already has the firmware)
rem   run.bat -FlashOnly only build and flash
set ROOT=%~dp0..\..
set PY=%ROOT%\.venv\Scripts\python.exe
if not exist "%PY%" (echo Missing .venv: run setup.bat first & exit /b 1)
if not exist "%ROOT%\config\wifi_secrets.h" (echo Missing config\wifi_secrets.h: copy config\wifi_secrets.h.example and fill it in & exit /b 1)
if /I not "%1"=="-NoFlash" (
  "%ROOT%\.venv\Scripts\pio.exe" run -d "%~dp0firmware" -t upload || exit /b 1
)
if /I "%1"=="-FlashOnly" exit /b 0
if "%GEMINI_API_KEY%"=="" (echo Missing GEMINI_API_KEY: setx GEMINI_API_KEY "your-key" and open a new terminal & exit /b 1)
if /I "%1"=="-NoFlash" shift
"%PY%" "%~dp0relay\main.py" %1 %2 %3 %4 %5 %6
