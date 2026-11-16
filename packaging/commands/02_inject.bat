@echo off
pushd "%~dp0"
copydog-loader.exe inject
set "COPYDOG_RESULT=%ERRORLEVEL%"
popd
pause
exit /b %COPYDOG_RESULT%
