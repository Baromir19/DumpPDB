@echo off
setlocal

set SCRIPT_DIR=%~dp0

echo Configuring DumpPDB solution (CMake preset: vs-x64)...
cmake --preset vs-x64 -S %SCRIPT_DIR%

if errorlevel 1 (
    echo.
    echo CMake configuration failed. See errors above.
    pause
    exit /b 1
)

echo.
echo Done. Open build\DumpPDB.sln in Visual Studio.
echo.

set /p OPEN_NOW="Open the solution now? [Y/n] "
if /I "%OPEN_NOW%"=="n" goto :end

start "" "%SCRIPT_DIR%build"

:end
endlocal