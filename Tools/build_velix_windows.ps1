$ErrorActionPreference = "Stop"

$Root = Get-Location
$Tools = "$Root\tools"
$Build = "$Root\build"
$Output = "$Root\output"

New-Item -ItemType Directory -Force -Path $Tools, $Build, $Output | Out-Null

$CMakeVersion = "4.2.3"
$LLVMVersion  = "18.1.8"

function Download($url, $out) {
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $out
}

$cmakeZip = "$Tools\cmake.zip"
Download "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" $cmakeZip
Expand-Archive $cmakeZip $Tools -Force
$CMakeDir = Get-ChildItem $Tools | Where-Object { $_.Name -like "cmake*" } | Select-Object -First 1

$ninjaExe = "$Tools\ninja.exe"
Download "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip" "$Tools\ninja.zip"
Expand-Archive "$Tools\ninja.zip" $Tools -Force

$llvmExe = "$Tools\llvm.exe"
Download "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVMVersion/LLVM-$LLVMVersion-win64.exe" $llvmExe

Start-Process -Wait -FilePath $llvmExe -ArgumentList "/S", "/D=$Tools\llvm"

$vulkanExe = "$Tools\vulkan.exe"
Download "https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe" $vulkanExe

Start-Process -Wait -FilePath $vulkanExe -ArgumentList "--accept-licenses --default-answer --confirm-command install --root $Tools\vulkan"

$env:PATH = "$($CMakeDir.FullName)\bin;$Tools;$Tools\llvm\bin;$Tools\vulkan\Bin;$env:PATH"
$env:VULKAN_SDK = "$Tools\vulkan"

cmake -S ../ -B $Build -G Ninja `
  -DCMAKE_C_COMPILER=clang-cl `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_BUILD_TYPE=Release

cmake --build $Build

Copy-Item "$Build\bin\*" $Output -Recurse -Force

Remove-Item $Tools -Recurse -Force

Write-Host "Build complete. Output in: $Output"
