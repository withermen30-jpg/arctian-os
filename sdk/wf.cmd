@echo off
rem wolf/wf — Arctian uygulama is akisinin tek kumandasi (Windows)
rem
rem   wolf new <ad> [baslik]  proje sablonu olustur (main.c + wolf.json)
rem   wolf build [proje]      main.c + wolf.json -> proje.wlf (imzasiz, yerel)
rem   wolf run ad.wlf         emulatorde calistir (pencereli)
rem   wolf shot ad.wlf [out]  headless PNG ekran goruntusu (varsayilan shot.png)
rem   wolf info ad.wlf        paket basligini yazdirir
rem   wolf check              araczincirini ve emulatoru denetler
rem   wolf help | -h | /?     bu liste
rem
rem (Windows'ta "wf" Work Folders konsoluyla cakisir; Linux/macOS'ta wf kullanilir)
setlocal
set SDK=%~dp0

set "EMU="
if defined LIBWOLF_EMU if exist "%LIBWOLF_EMU%" set "EMU=%LIBWOLF_EMU%"
if not defined EMU for %%d in (
  "%SDK%bin\arctian-emu.exe"
  "%SDK%..\build\emu-win\arctian-emu.exe"
  "%LOCALAPPDATA%\Programs\libWolf\bin\arctian-emu.exe"
) do if not defined EMU if exist %%d set "EMU=%%~d"
if not defined EMU where arctian-emu >nul 2>nul && set "EMU=arctian-emu"

set "PY=python"
where python >nul 2>nul || set "PY=py"

set CMD=%~1
if "%CMD%"=="" set CMD=help
if "%CMD%"=="-h" set CMD=help
if "%CMD%"=="--help" set CMD=help
if "%CMD%"=="/?" set CMD=help
if "%CMD%"=="-?" set CMD=help
if "%CMD%"=="?" set CMD=help
rem not: cmd'de shift %*'yi kaydirMAZ; alt komutlar %2.. ile alir

if "%CMD%"=="new" (
  if "%~2"=="" ( echo kullanim: wolf new ^<ad^> [baslik] & exit /b 2 )
  "%PY%" "%SDK%wlfpack.py" --new %2 --title "%~3"
  goto :eof
)
if "%CMD%"=="build" (
  rem .wlf ELF/SysV uretir; Windows derlemesi WSL uzerinden gider
  call "%SDK%build.bat" %2
  goto :eof
)
if "%CMD%"=="run" (
  if "%~2"=="" ( echo kullanim: wf run uygulama.wlf [-- arg] & exit /b 2 )
  if not defined EMU ( echo [wf] emulator bulunamadi: arctian-emu.exe yok & exit /b 1 )
  "%EMU%" %2 %3 %4 %5 %6 %7 %8 %9
  goto :eof
)
if "%CMD%"=="shot" (
  if "%~2"=="" ( echo kullanim: wf shot uygulama.wlf [cikti.png] & exit /b 2 )
  if not defined EMU ( echo [wf] emulator bulunamadi & exit /b 1 )
  call :doshot %2 %3
  goto :eof
)
if "%CMD%"=="info" (
  if "%~2"=="" ( echo kullanim: wf info uygulama.wlf & exit /b 2 )
  "%PY%" "%SDK%wlfpack.py" --inspect %2
  goto :eof
)
if "%CMD%"=="check" (
  "%PY%" --version >nul 2>nul && echo [ok] python
  if defined EMU (echo [ok] emulator: %EMU%) else (echo [uyari] emulator bulunamadi)
  gcc --version >nul 2>nul && echo [ok] gcc || echo [uyari] gcc yok ^(w64devkit PATH'te degil^)
  goto :eof
)
if "%CMD%"=="help" (
  echo wolf new ^<ad^> [baslik]  proje sablonu olustur ^(main.c + wolf.json^)
  echo wolf build ^[proje^]      .wlf uret ^(WSL uzerinden^)
  echo wolf run ^<wlf^>          emulatorde calistir ^(pencereli^)
  echo wolf shot ^<wlf^> [png]   ekran goruntusu al, varsayilan shot.png
  echo wolf info ^<wlf^>         paket bilgisi + imza durumu
  echo wolf check              araczincirini denetle ^(python/gcc/emulator^)
  echo.
  echo Windows'ta "wf" komutu Windows'un Work Folders konsoluyla cakisir;
  echo bu yuzden Windows kumandasi "wolf"^'tur. Linux/macOS'ta: wf.
  goto :eof
)
echo [wf] bilinmeyen komut: %CMD%
exit /b 2

:doshot
rem wf shot uygulama.wlf [cikti.png] — headless PNG (varsayilan shot.png)
set "WLF=%~1"
set "OUT=%~2"
if "%OUT%"=="" set "OUT=shot.png"
set "HB=%SDK%bin\arctian-emu-headless.exe"
if not exist "%HB%" set "HB=%SDK%..\build\emu-win\arctian-emu-headless.exe"
if not exist "%HB%" set "HB=%EMU%"
"%HB%" "%WLF%" --headless --frames 90 --shot "%OUT%"
exit /b %ERRORLEVEL%

:runbuild
rem Windows'ta build.sh POSIX sh ister; w64devkit sh'i varsa onu kullan.
where sh >nul 2>nul && ( sh "%SDK%build.sh" %* & goto :eof )
where busybox >nul 2>nul && ( busybox sh "%SDK%build.sh" %* & goto :eof )
echo [wf] sh bulunamadi; w64devkit PATH'e ekleyin (C:\w64devkit\bin)
exit /b 1
