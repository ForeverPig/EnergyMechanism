@echo off
cd /d "%~dp0.."
python tools\serial_monitor.py --port COM3
if errorlevel 1 pause
