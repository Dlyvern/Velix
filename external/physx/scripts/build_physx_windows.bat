@echo off
setlocal enabledelayedexpansion

:: Configuration
set PHYSX_BUILD_TYPE=%~1
if "%PHYSX_BUILD_TYPE%"=="" set PHYSX_BUILD_TYPE=checked

set PHYSX_MSBUILD_CONFIGURATION=
if /I "%PHYSX_BUILD_TYPE%"=="debug" set PHYSX_MSBUILD_CONFIGURATION=Debug
if /I "%PHYSX_BUILD_TYPE%"=="checked" set PHYSX_MSBUILD_CONFIGURATION=Checked
if /I "%PHYSX_BUILD_TYPE%"=="profile" set PHYSX_MSBUILD_CONFIGURATION=Profile
if /I "%PHYSX_BUILD_TYPE%"=="release" set PHYSX_MSBUILD_CONFIGURATION=Release

if "%PHYSX_MSBUILD_CONFIGURATION%"=="" (
    echo [VelixFlow] Error: Unsupported PhysX build type "%PHYSX_BUILD_TYPE%"
    exit /b 1
)

for %%I in ("%~dp0..") do set PHYSX_EXTERNAL_ROOT=%%~fI
set PHYSX_ROOT=%PHYSX_EXTERNAL_ROOT%\PhysX\physx
set PHYSX_LIB_OUTPUT=%PHYSX_EXTERNAL_ROOT%\lib\windows\%PHYSX_BUILD_TYPE%

cd "%PHYSX_ROOT%"

echo [VelixFlow] Generating Visual Studio project files...
call generate_projects.bat vc17win64-cpu-only

echo [VelixFlow] Building PhysX in %PHYSX_BUILD_TYPE% mode...
cd "%PHYSX_ROOT%\compiler\vc17win64-cpu-only"

msbuild PhysXSDK.sln /p:Configuration=%PHYSX_MSBUILD_CONFIGURATION%

if errorlevel 1 (
    echo [VelixFlow] Error: Failed to build PhysX
    exit /b 1
)

echo [VelixFlow] Copying built PhysX binaries to %PHYSX_LIB_OUTPUT%...
mkdir "%PHYSX_LIB_OUTPUT%" 2>nul

robocopy "%PHYSX_ROOT%\bin\win.x86_64.vc143.mt/%PHYSX_BUILD_TYPE%" "%PHYSX_LIB_OUTPUT%" *.lib *.dll *.pdb /njh /njs /ndl /nc /ns /np >nul
set ROBOCOPY_EXIT=%ERRORLEVEL%
if %ROBOCOPY_EXIT% GEQ 8 (
    echo [VelixFlow] Error: Failed to copy PhysX binaries (robocopy exit %ROBOCOPY_EXIT%)
    exit /b 1
)

echo [VelixFlow] PhysX built and installed to %PHYSX_LIB_OUTPUT%
exit /b 0
