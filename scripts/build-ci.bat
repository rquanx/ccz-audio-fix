@echo off
setlocal
cd /d "%~dp0\.."

if not exist build mkdir build
if not exist release mkdir release

cl /nologo /O2 /W4 /LD /DUNICODE /D_UNICODE ^
  /I src\common ^
  /Fo:build\audio_hook.obj ^
  src\hook\audio_hook.c ^
  /link /OUT:build\ccz_audio_hook.dll ^
  /IMPLIB:build\ccz_audio_hook.lib
if errorlevel 1 exit /b %errorlevel%

rc /nologo /fo build\launcher.res resources\launcher.rc
if errorlevel 1 exit /b %errorlevel%

cl /nologo /utf-8 /O2 /W4 /DUNICODE /D_UNICODE /c ^
  /I src\common /I src\launcher ^
  /Fo:build\main.obj src\launcher\main.c
if errorlevel 1 exit /b %errorlevel%

cl /nologo /utf-8 /O2 /W4 /DUNICODE /D_UNICODE /c ^
  /I src\common /I src\launcher ^
  /Fo:build\config.obj src\launcher\config.c
if errorlevel 1 exit /b %errorlevel%

cl /nologo /O2 /W4 /DUNICODE /D_UNICODE /c ^
  /I src\common /I src\launcher ^
  /Fo:build\injector.obj src\launcher\injector.c
if errorlevel 1 exit /b %errorlevel%

link /nologo /SUBSYSTEM:WINDOWS ^
  /OUT:release\Ekd5-audio-fix.exe ^
  build\main.obj build\config.obj build\injector.obj ^
  build\launcher.res user32.lib shell32.lib
