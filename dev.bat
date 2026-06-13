@echo off
setlocal EnableExtensions

set "REPO=%~dp0"
set "REPO=%REPO:~0,-1%"
set "UE_ROOT=C:\EpicGames\UE_5.6"
set "PROJECT=C:\Users\luisarandas\Documents\Unreal Projects\character2\character2.uproject"
set "BUILD=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "METAAGENT=%REPO%\metaagent"
set "METAAGENT_BUILD=%METAAGENT%\build"

if "%~1"=="" goto dev
if /i "%~1"=="dev" goto dev
if /i "%~1"=="core" goto core
if /i "%~1"=="test" goto test
if /i "%~1"=="build" goto build
if /i "%~1"=="plugin" goto plugin
if /i "%~1"=="launch" goto launch
if /i "%~1"=="all" goto all

echo Usage: %~nx0 [dev^|core^|test^|build^|plugin^|launch^|all]
echo.
echo   dev     - Default. Build metaagent core + run tests + build UE plugin
echo   core    - Configure and build metaagent only (CMake)
echo   test    - Run metaagent unit tests (builds core first if needed)
echo   plugin  - Build UE editor target (recompiles changed plugin modules)
echo   build   - Same as plugin (full character2Editor target)
echo   launch  - Open the project in Unreal Editor
echo   all     - dev, then launch the editor
exit /b 1

:dev
call "%~f0" core
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~f0" test
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~f0" plugin
exit /b %ERRORLEVEL%

:core
echo.
echo === metaagent core (CMake) ===
if not exist "%METAAGENT_BUILD%" (
    cmake -S "%METAAGENT%" -B "%METAAGENT_BUILD%" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b %ERRORLEVEL%
)
cmake --build "%METAAGENT_BUILD%" --config Release
exit /b %ERRORLEVEL%

:test
echo.
echo === metaagent tests ===
if not exist "%METAAGENT_BUILD%" (
    call "%~f0" core
    if errorlevel 1 exit /b %ERRORLEVEL%
)
ctest --test-dir "%METAAGENT_BUILD%" -C Release --output-on-failure
exit /b %ERRORLEVEL%

:build
goto plugin

:plugin
echo.
echo === UE project (MetaAgent plugin modules) ===
cd /d "%UE_ROOT%"
call "%BUILD%" character2Editor Win64 Development "%PROJECT%" -waitmutex
if errorlevel 1 (
    echo.
    echo UE build failed. Close Unreal Editor / disable Live Coding, then retry.
    exit /b %ERRORLEVEL%
)
exit /b 0

:launch
start "" "%EDITOR%" "%PROJECT%"
exit /b 0

:all
call "%~f0" dev
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~f0" launch
exit /b 0
