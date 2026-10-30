@echo off
REM build.bat - Arctian .wlf derleyicisi (Windows, WSL uzerinden)

setlocal

where wsl >nul 2>&1
if errorlevel 1 (
  echo [hata] WSL bulunamadi. WSL kurun:  wsl --install
  exit /b 1
)

if not "%~1"=="" (
  set "PROJ=%~1"
) else (
  set "PROJ=%CD%"
)

REM trailing backslash sorununu onle (wslpath tirnagi bozar)
set "SDKDIR=%~dp0"
if "%SDKDIR:~-1%"=="\" set "SDKDIR=%SDKDIR:~0,-1%"

for /f "usebackq delims=" %%p in (`wsl wslpath -a "%PROJ%"`) do set "WSLPROJ=%%p"
for /f "usebackq delims=" %%p in (`wsl wslpath -a "%SDKDIR%"`) do set "WSLSDK=%%p"

echo [WSL] derleniyor: %WSLPROJ%
wsl bash -c "cd '%WSLPROJ%' && SDK='%WSLSDK%' bash '%WSLSDK%/build.sh'"
if errorlevel 1 (
  echo.
  echo [hata] derleme basarisiz.
  exit /b 1
)
exit /b 0