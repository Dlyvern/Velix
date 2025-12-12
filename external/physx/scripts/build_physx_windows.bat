@echo off
setlocal enabledelayedexpansion

:: Configuration
set PHYSX_BUILD_TYPE=checked
set PHYSX_EXTERNAL_ROOT=%cd%\physx
set PHYSX_ROOT=%PHYSX_EXTERNAL_ROOT%\PhysX\physx
set PHYSX_LIB_OUTPUT=%PHYSX_EXTERNAL_ROOT%\lib\windows\%PHYSX_BUILD_TYPE%

cd "%PHYSX_ROOT%"

echo [VelixFlow] Generating Visual Studio project files...
call generate_projects.bat vc17win64-cpu-only

echo [VelixFlow] Building PhysX in %PHYSX_BUILD_TYPE% mode...
cd "%PHYSX_ROOT%\compiler\vc17win64-cpu-only"

msbuild PhysXSDK.sln /p:Configuration=Checked

if errorlevel 1 (
    echo [VelixFlow] Error: Failed to build PhysX
    exit /b 1
)

echo [VelixFlow] Moving built .lib files to %PHYSX_LIB_OUTPUT%...
mkdir "%PHYSX_LIB_OUTPUT%" 2>nul

robocopy "%PHYSX_ROOT%\bin\win.x86_64.vc143.mt/%PHYSX_BUILD_TYPE%" "%PHYSX_LIB_OUTPUT%" *.lib /njh /njs /ndl /nc /ns /np >nul

echo [VelixFlow] PhysX built and installed to %PHYSX_LIB_OUTPUT%