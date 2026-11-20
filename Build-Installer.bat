@echo off
setlocal
pushd "%~dp0" || exit /b 1
where py >nul 2>&1
if errorlevel 1 goto usepython
py -3 build.py --installer
goto done
:usepython
python build.py --installer
:done
set "result=%errorlevel%"
if "%result%"=="0" echo Installer ready in dist.
if not defined CI pause
popd
exit /b %result%
