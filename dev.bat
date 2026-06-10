@echo off
setlocal

set "UE_ROOT=C:\EpicGames\UE_5.6"
set "PROJECT=C:\Users\luisarandas\Documents\Unreal Projects\character2\character2.uproject"
set "BUILD=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "EDITOR=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"

if /i "%~1"=="build" goto build
if /i "%~1"=="plugin" goto plugin
if /i "%~1"=="launch" goto launch
if /i "%~1"=="all" goto all

echo Usage: %~nx0 [build^|plugin^|launch^|all]
echo.
echo   build   - Build game project + editor + plugin
echo   plugin  - Build plugin only
echo   launch  - Open the project in Unreal Editor
echo   all     - Build everything, then launch the editor
exit /b 1

:build
cd /d "%UE_ROOT%"
call "%BUILD%" character2Editor Win64 Development "%PROJECT%" -waitmutex
exit /b %ERRORLEVEL%

:plugin
cd /d "%UE_ROOT%"
call "%BUILD%" MetaAgentPluginEditor Win64 Development "%PROJECT%" -waitmutex
exit /b %ERRORLEVEL%

:launch
start "" "%EDITOR%" "%PROJECT%"
exit /b 0

:all
call "%~f0" build
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~f0" launch
exit /b 0
