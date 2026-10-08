@echo off

where cl >nul 2>nul
if not errorlevel 1 exit /b 0

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
  echo Visual Studio with Desktop development with C++ is required.
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "vs_install=%%i"
)
if not defined vs_install (
  echo Visual Studio C++ x86 tools were not found.
  exit /b 1
)

call "%vs_install%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
exit /b %errorlevel%
