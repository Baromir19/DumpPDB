@echo off
setlocal

set SCRIPT_DIR=%~dp0

echo Configuring DumpPDB solution (CMake preset: vs2022-x64)...
cmake --preset vs2022-x64 -S %SCRIPT_DIR%

if errorlevel 1 (
    echo.
    echo CMake configuration failed. See errors above.
    pause
    exit /b 1
)

echo.
echo Done. Open build\DumpPDB_Solution.sln in Visual Studio.
echo.

set /p OPEN_NOW="Open the solution now? [Y/n] "
if /I "%OPEN_NOW%"=="n" goto :end

start "" "%SCRIPT_DIR%build\DumpPDB.sln"

:end
endlocal