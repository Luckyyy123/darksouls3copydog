@echo off
pushd "%~dp0"
copydog-loader.exe preflight
set "COPYDOG_RESULT=%ERRORLEVEL%"
popd
pause
exit /b %COPYDOG_RESULT%
