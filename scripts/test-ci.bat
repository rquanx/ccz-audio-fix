@echo off
setlocal
cd /d "%~dp0\.."

if not exist .test-run mkdir .test-run
cl /nologo /utf-8 /O2 /W4 /DUNICODE /D_UNICODE /c ^
  /I src\launcher ^
  /Fo:.test-run\config_test.obj ^
  tests\config_test.c
if errorlevel 1 exit /b %errorlevel%

cl /nologo /utf-8 /O2 /W4 /DUNICODE /D_UNICODE /c ^
  /I src\launcher ^
  /Fo:.test-run\config.obj ^
  src\launcher\config.c
if errorlevel 1 exit /b %errorlevel%

link /nologo /SUBSYSTEM:CONSOLE ^
  /OUT:.test-run\config_test.exe ^
  .test-run\config_test.obj .test-run\config.obj shell32.lib
if errorlevel 1 exit /b %errorlevel%

pushd .test-run
config_test.exe
set test_result=%errorlevel%
popd
exit /b %test_result%
