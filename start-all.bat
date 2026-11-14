@echo off
cd /d "%~dp0"
set "PY=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
if not exist "%PY%" set "PY=python"
echo Using Python: %PY%
start "eat-agent-8787" cmd /k "%PY%" -m agent --host 127.0.0.1 --port 8787
start "eat-web-4173" cmd /k "%PY%" -m http.server 4173 --bind 127.0.0.1
echo.
echo Agent backend: http://127.0.0.1:8787
echo Web page:      http://127.0.0.1:4173
echo Keep both windows open. Closing them stops the services.
echo If a window shows an error, send me a screenshot of it.
pause