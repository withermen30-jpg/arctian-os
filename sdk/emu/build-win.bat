@echo off
rem Arctian Emulator — Windows derlemesi (C:\w64devkit gerekir).
rem Kullanim: build-win.bat  (opsiyonel: hedef, orn. "all")
setlocal
set PATH=C:\w64devkit\bin;%PATH%
cd /d "%~dp0"

where python >nul 2>nul
if %errorlevel%==0 (
  python ..\..\tools\emu_srcs.py --write gen
) else (
  echo [uyari] python bulunamadi; mevcut sdk\emu\gen anlik goruntusu kullanilacak
)

mingw32-make -f Makefile.win -j%NUMBER_OF_PROCESSORS% %*
if errorlevel 1 exit /b 1
echo.
echo Hazir: build\emu-win\arctian-emu.exe  +  arctian-emu-headless.exe
