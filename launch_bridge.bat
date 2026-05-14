@echo off
REM Launch the PC-side CAN bridge for ESP32-S3-BOX-3 motor controller.
REM Double-click this file (or a shortcut to it) to start bridge.py.

cd /d "%~dp0"
python bridge.py

REM Keep the console open so errors stay readable after the script exits.
pause
