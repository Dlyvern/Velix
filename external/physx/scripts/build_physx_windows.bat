@echo off
setlocal enabledelayedexpansion

:: Configuration
set PHYSX_BUILD_TYPE=%~1
if "%PHYSX_BUILD_TYPE%"=="" set PHYSX_BUILD_TYPE=checked
set "PHYSX_PRESET_BASE=vc17win64-cpu-only"
set "PHYSX_PRESET=%PHYSX_PRESET_BASE%-velix-ci"

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
set "PHYSX_ROOT=%PHYSX_EXTERNAL_ROOT%\PhysX\physx"
set "PHYSX_LIB_OUTPUT=%PHYSX_EXTERNAL_ROOT%\lib\windows\%PHYSX_BUILD_TYPE%"
set "PHYSX_PRESET_TEMPLATE=%PHYSX_ROOT%\buildtools\presets\public\%PHYSX_PRESET_BASE%.xml"
set "PHYSX_PRESET_XML=%PHYSX_ROOT%\buildtools\presets\%PHYSX_PRESET%.xml"
set "PHYSX_COMPILER_DIR=%PHYSX_ROOT%\compiler\%PHYSX_PRESET%"
set "PHYSX_SOLUTION=%PHYSX_COMPILER_DIR%\PhysXSDK.sln"
set "PHYSX_BUILD_OUTPUT="

pushd "%PHYSX_ROOT%"

echo [VelixFlow] Python version:
python --version 2>&1 || echo [VelixFlow] WARNING: python not found in PATH

if not exist "%PHYSX_PRESET_TEMPLATE%" (
    echo [VelixFlow] Error: PhysX preset template not found at "%PHYSX_PRESET_TEMPLATE%"
    popd
    exit /b 1
)

echo [VelixFlow] Preparing PhysX preset %PHYSX_PRESET%...
python -c "from pathlib import Path; template = Path(r'%PHYSX_PRESET_TEMPLATE%'); output = Path(r'%PHYSX_PRESET_XML%'); text = template.read_text(); text = text.replace('name=\"%PHYSX_PRESET_BASE%\"', 'name=\"%PHYSX_PRESET%\"', 1); text = text.replace('name=\"PX_BUILDSNIPPETS\" value=\"True\"', 'name=\"PX_BUILDSNIPPETS\" value=\"False\"', 1); text = text.replace('name=\"PX_BUILDPVDRUNTIME\" value=\"True\"', 'name=\"PX_BUILDPVDRUNTIME\" value=\"False\"', 1); output.write_text(text)"
if errorlevel 1 (
    echo [VelixFlow] Error: Failed to write temporary PhysX preset
    popd
    exit /b 1
)

echo [VelixFlow] Generating Visual Studio project files...
call generate_projects.bat %PHYSX_PRESET% 2>&1
if errorlevel 1 (
    echo [VelixFlow] Error: Failed to generate PhysX Visual Studio projects
    echo [VelixFlow] Available presets:
    dir /b compiler 2>nul
    if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
    popd
    exit /b 1
)

if not exist "%PHYSX_SOLUTION%" (
    echo [VelixFlow] Error: Generated PhysX solution not found at "%PHYSX_SOLUTION%"
    if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
    popd
    exit /b 1
)

echo [VelixFlow] Building PhysX in %PHYSX_BUILD_TYPE% mode...
msbuild "%PHYSX_SOLUTION%" /p:Configuration=%PHYSX_MSBUILD_CONFIGURATION% /maxcpucount /t:Build /v:n /nologo
set MSBUILD_EXIT=%ERRORLEVEL%

if %MSBUILD_EXIT% NEQ 0 (
    echo [VelixFlow] Error: Failed to build PhysX (MSBuild exit code: %MSBUILD_EXIT%)
    if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
    popd
    exit /b 1
)

for /f "delims=" %%F in ('dir /b /s "%PHYSX_ROOT%\bin\PhysX_64.lib" 2^>nul') do (
    set "PHYSX_BUILD_OUTPUT=%%~dpF"
    goto :BUILD_OUTPUT_FOUND
)

for /f "delims=" %%F in ('dir /b /s "%PHYSX_ROOT%\bin\PhysX_static_64.lib" 2^>nul') do (
    set "PHYSX_BUILD_OUTPUT=%%~dpF"
    goto :BUILD_OUTPUT_FOUND
)

for /f "delims=" %%F in ('dir /b /s "%PHYSX_ROOT%\bin\PhysXFoundation_64.lib" 2^>nul') do (
    set "PHYSX_BUILD_OUTPUT=%%~dpF"
    goto :BUILD_OUTPUT_FOUND
)

for /f "delims=" %%F in ('dir /b /s "%PHYSX_ROOT%\bin\PhysXFoundation_static_64.lib" 2^>nul') do (
    set "PHYSX_BUILD_OUTPUT=%%~dpF"
    goto :BUILD_OUTPUT_FOUND
)

rem Fallback: find any _static_64.lib produced under bin\
for /f "delims=" %%F in ('dir /b /s "%PHYSX_ROOT%\bin\*_static_64.lib" 2^>nul') do (
    set "PHYSX_BUILD_OUTPUT=%%~dpF"
    goto :BUILD_OUTPUT_FOUND
)

:BUILD_OUTPUT_FOUND
if defined PHYSX_BUILD_OUTPUT (
    for %%D in ("%PHYSX_BUILD_OUTPUT%\.") do set "PHYSX_BUILD_OUTPUT=%%~fD"
)

if not defined PHYSX_BUILD_OUTPUT (
    echo [VelixFlow] Error: PhysX build output directory was not found under "%PHYSX_ROOT%\bin"
    dir /b "%PHYSX_ROOT%\bin" 2>nul
    dir /b /s "%PHYSX_ROOT%\bin\*.lib" 2>nul
    if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
    popd
    exit /b 1
)

echo [VelixFlow] Using PhysX build output from %PHYSX_BUILD_OUTPUT%
echo [VelixFlow] Copying built PhysX binaries to %PHYSX_LIB_OUTPUT%...
mkdir "%PHYSX_LIB_OUTPUT%" 2>nul

robocopy "%PHYSX_BUILD_OUTPUT%" "%PHYSX_LIB_OUTPUT%" *.lib *.dll *.pdb /njh /njs /ndl /nc /ns /np
set ROBOCOPY_EXIT=%ERRORLEVEL%
if %ROBOCOPY_EXIT% GEQ 8 (
    echo [VelixFlow] Error: Failed to copy PhysX binaries (robocopy exit %ROBOCOPY_EXIT%)
    if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
    popd
    exit /b 1
)

if exist "%PHYSX_PRESET_XML%" del /q "%PHYSX_PRESET_XML%"
echo [VelixFlow] PhysX built and installed to %PHYSX_LIB_OUTPUT%
popd
exit /b 0
