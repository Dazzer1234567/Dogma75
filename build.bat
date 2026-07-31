@echo off
echo Building Minimal DAW...
echo.

REM Find Visual Studio 2022
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set "VSINSTALLDIR=%%i"
)

if not defined VSINSTALLDIR (
    echo Visual Studio 2022 not found!
    pause
    exit /b 1
)

echo Found Visual Studio at: %VSINSTALLDIR%
echo.

REM Setup Visual Studio environment
call "%VSINSTALLDIR%\Common7\Tools\VsDevCmd.bat"

echo.
echo Creating build directory...
if not exist build mkdir build
cd build

echo.
echo Running CMake configuration with ASIO enabled...
cmake -G "Visual Studio 18 2026" -DPA_USE_ASIO=ON ..

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo Building project...
cmake --build . --config Debug

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo Copying PortAudio DLL...
copy /Y external\portaudio\Debug\portaudio.dll Debug\portaudio.dll

echo.
echo ========================================
echo Build complete!
echo ASIO support: ENABLED
echo ========================================
echo.
echo Run the program with:
echo   Debug\MinimalDAW.exe
echo.

cd ..
pause
