$ErrorActionPreference = "Stop"

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir

$BuildDir = Join-Path $Root "build"
$OutputDir = Join-Path $Root "output"
$ToolCacheDir = Join-Path $Root ".velix-tools"
$DownloadsDir = Join-Path $ToolCacheDir "downloads"
$CMakeExtractDir = Join-Path $ToolCacheDir "cmake"
$NinjaDir = Join-Path $ToolCacheDir "ninja"
$VulkanInstallDir = Join-Path $ToolCacheDir "vulkan"

$CMakeVersion = "4.2.3"
$NinjaDownloadUrl = "https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip"
$VulkanDownloadUrl = "https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe"
$BuildType = "Release"

function Show-Usage {
    Write-Host "Usage:"
    Write-Host "  ./Tools/build_velix_windows.ps1 --build-all"
    Write-Host "  ./Tools/build_velix_windows.ps1 --build-clean"
    Write-Host "  ./Tools/build_velix_windows.ps1 --build"
    Write-Host ""
    Write-Host "Modes:"
    Write-Host "  --build-all   Redownload local build tools, remove build/output, then configure, build and install."
    Write-Host "  --build-clean Reuse local build tools, remove build/output, then configure, build and install."
    Write-Host "  --build       Reuse local build tools and existing build directory when possible, then build and install."
    Write-Host ""
    Write-Host "Note: Visual Studio Build Tools with Desktop C++ support must already be installed."
}

function Write-Step([string]$Message) {
    Write-Host "==> $Message"
}

function Remove-DirectoryIfExists([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Ensure-Directory([string]$Path) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Download-File([string]$Url, [string]$OutFile, [switch]$Force) {
    if ((-not $Force) -and (Test-Path -LiteralPath $OutFile)) {
        Write-Step "Using cached download $OutFile"
        return
    }

    Ensure-Directory (Split-Path -Parent $OutFile)
    Write-Step "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile
}

function Find-DirectoryContainingFile([string]$RootPath, [string]$RelativeFile) {
    if (-not (Test-Path -LiteralPath $RootPath)) {
        return $null
    }

    $rootItem = Get-Item -LiteralPath $RootPath
    $rootCandidate = Join-Path $rootItem.FullName $RelativeFile
    if (Test-Path -LiteralPath $rootCandidate) {
        return $rootItem.FullName
    }

    $directories = Get-ChildItem -LiteralPath $RootPath -Directory -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName

    foreach ($directory in $directories) {
        $candidate = Join-Path $directory.FullName $RelativeFile
        if (Test-Path -LiteralPath $candidate) {
            return $directory.FullName
        }
    }

    return $null
}

function Get-VulkanSdkSearchRoots {
    $roots = @($VulkanInstallDir)

    if ($env:VULKAN_SDK) {
        $roots += $env:VULKAN_SDK
    }

    $systemDrive = if ($env:SystemDrive) { $env:SystemDrive } else { "C:" }
    $roots += (Join-Path $systemDrive "VulkanSDK")

    if ($env:ProgramFiles) {
        $roots += (Join-Path $env:ProgramFiles "VulkanSDK")
    }

    if (${env:ProgramFiles(x86)}) {
        $roots += (Join-Path ${env:ProgramFiles(x86)} "VulkanSDK")
    }

    if ($env:LOCALAPPDATA) {
        $roots += (Join-Path $env:LOCALAPPDATA "Programs\VulkanSDK")
        $roots += (Join-Path $env:LOCALAPPDATA "VulkanSDK")
    }

    if ($env:USERPROFILE) {
        $roots += (Join-Path $env:USERPROFILE "VulkanSDK")
    }

    $glslangValidator = Get-Command glslangValidator.exe -ErrorAction SilentlyContinue
    if ($glslangValidator) {
        $glslangDir = Split-Path -Parent $glslangValidator.Source
        if ($glslangDir) {
            $roots += (Split-Path -Parent $glslangDir)
        }
    }

    return $roots |
        Where-Object { $_ -and $_.Trim() -ne "" } |
        Select-Object -Unique
}

function Get-VulkanSdkRootsFromRegistry {
    $registryPaths = @(
        "HKLM:\SOFTWARE\Khronos\Vulkan\SDK",
        "HKCU:\SOFTWARE\Khronos\Vulkan\SDK",
        "HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\SDK",
        "HKCU:\SOFTWARE\WOW6432Node\Khronos\Vulkan\SDK"
    )

    $roots = @()
    foreach ($registryPath in $registryPaths) {
        if (-not (Test-Path -LiteralPath $registryPath)) {
            continue
        }

        $properties = Get-ItemProperty -LiteralPath $registryPath
        foreach ($property in $properties.PSObject.Properties) {
            if ($property.Name.StartsWith("PS")) {
                continue
            }

            $value = [string]$property.Value
            if (-not [string]::IsNullOrWhiteSpace($value)) {
                $roots += $value
            }
        }
    }

    return $roots |
        Where-Object { $_ -and $_.Trim() -ne "" } |
        Select-Object -Unique
}

function Find-VulkanSdkRoot {
    $searchRoots = @()
    $searchRoots += Get-VulkanSdkSearchRoots
    $searchRoots += Get-VulkanSdkRootsFromRegistry

    foreach ($searchRoot in ($searchRoots | Select-Object -Unique)) {
        $sdkRoot = Find-DirectoryContainingFile $searchRoot "Include\vulkan\vulkan.h"
        if ($sdkRoot) {
            return $sdkRoot
        }
    }

    return $null
}

function Ensure-CMake([switch]$ForceRefresh) {
    $cmakeExe = Find-DirectoryContainingFile $CMakeExtractDir "bin\cmake.exe"
    if ((-not $ForceRefresh) -and $cmakeExe) {
        return $cmakeExe
    }

    Write-Step "Preparing bundled CMake $CMakeVersion"
    Remove-DirectoryIfExists $CMakeExtractDir
    Ensure-Directory $CMakeExtractDir

    $cmakeZip = Join-Path $DownloadsDir "cmake-$CMakeVersion-windows-x86_64.zip"
    $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip"
    Download-File $cmakeUrl $cmakeZip -Force:$ForceRefresh
    Expand-Archive -Path $cmakeZip -DestinationPath $CMakeExtractDir -Force

    $cmakeRoot = Find-DirectoryContainingFile $CMakeExtractDir "bin\cmake.exe"
    if (-not $cmakeRoot) {
        throw "Bundled CMake was downloaded, but cmake.exe was not found under $CMakeExtractDir"
    }

    return $cmakeRoot
}

function Ensure-Ninja([switch]$ForceRefresh) {
    $ninjaExe = Join-Path $NinjaDir "ninja.exe"
    if ((-not $ForceRefresh) -and (Test-Path -LiteralPath $ninjaExe)) {
        return $ninjaExe
    }

    Write-Step "Preparing bundled Ninja"
    Remove-DirectoryIfExists $NinjaDir
    Ensure-Directory $NinjaDir

    $ninjaZip = Join-Path $DownloadsDir "ninja-win.zip"
    Download-File $NinjaDownloadUrl $ninjaZip -Force:$ForceRefresh
    Expand-Archive -Path $ninjaZip -DestinationPath $NinjaDir -Force

    if (-not (Test-Path -LiteralPath $ninjaExe)) {
        throw "Bundled Ninja was downloaded, but ninja.exe was not found at $ninjaExe"
    }

    return $ninjaExe
}

function Ensure-VulkanSdk([switch]$ForceRefresh) {
    $sdkRoot = Find-VulkanSdkRoot
    if ((-not $ForceRefresh) -and $sdkRoot) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot "Bin\glslangValidator.exe"))) {
            throw "Vulkan SDK was found at $sdkRoot, but Bin\glslangValidator.exe is missing."
        }
        return $sdkRoot
    }

    Write-Step "Preparing bundled Vulkan SDK"
    Remove-DirectoryIfExists $VulkanInstallDir
    Ensure-Directory $VulkanInstallDir

    $vulkanInstaller = Join-Path $DownloadsDir "vulkan-sdk.exe"
    Download-File $VulkanDownloadUrl $vulkanInstaller -Force:$ForceRefresh

    $installAttempts = @(
        [pscustomobject]@{
            Name = "local tool cache"
            LaunchMode = "StartProcess"
            Arguments = @(
                "--accept-licenses",
                "--default-answer",
                "--confirm-command",
                "install",
                "--root", $VulkanInstallDir
            )
        },
        [pscustomobject]@{
            Name = "default VulkanSDK location"
            LaunchMode = "StartProcess"
            Arguments = @(
                "--accept-licenses",
                "--default-answer",
                "--confirm-command",
                "install"
            )
        },
        [pscustomobject]@{
            Name = "default VulkanSDK location (direct invocation)"
            LaunchMode = "Direct"
            Arguments = @(
                "--accept-licenses",
                "--default-answer",
                "--confirm-command",
                "install"
            )
        }
    )

    $attemptResults = @()
    foreach ($attempt in $installAttempts) {
        Write-Step "Running Vulkan SDK installer via $($attempt.Name)"
        $exitCode = $null
        if ($attempt.LaunchMode -eq "Direct") {
            & $vulkanInstaller @($attempt.Arguments)
            $exitCode = $LASTEXITCODE
        }
        else {
            $installerProcess = Start-Process -Wait -PassThru -FilePath $vulkanInstaller -ArgumentList $attempt.Arguments
            $exitCode = $installerProcess.ExitCode
        }

        $attemptResults += "$($attempt.Name): exit code $exitCode"

        $sdkRoot = $null
        $deadline = (Get-Date).AddSeconds(30)
        do {
            $sdkRoot = Find-VulkanSdkRoot
            if ($sdkRoot) {
                break
            }
            Start-Sleep -Seconds 2
        } while ((Get-Date) -lt $deadline)

        if ($sdkRoot) {
            if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot "Bin\glslangValidator.exe"))) {
                throw "Vulkan SDK was installed at $sdkRoot, but Bin\glslangValidator.exe is missing."
            }

            if ($exitCode -ne 0) {
                Write-Warning "Vulkan SDK installer exited with code $exitCode, but a usable SDK was found at $sdkRoot."
            }
            return $sdkRoot
        }

        if ($exitCode -ne 0) {
            Write-Warning "Vulkan SDK installer attempt '$($attempt.Name)' exited with code $exitCode. Trying the next fallback."
        }
    }

    $searchedRoots = @()
    $searchedRoots += Get-VulkanSdkSearchRoots
    $searchedRoots += Get-VulkanSdkRootsFromRegistry
    $searchedRoots = ($searchedRoots | Select-Object -Unique) -join ", "
    $attemptSummary = $attemptResults -join "; "
    throw "Bundled Vulkan SDK installer did not produce a usable SDK. Attempts: $attemptSummary. Searched: $searchedRoots. Install Vulkan SDK manually, then re-run this script with VULKAN_SDK set to the SDK root if needed."
}

function Import-VisualStudioBuildEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        Write-Step "Using existing MSVC environment from PATH"
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "MSVC tools were not found on PATH and vswhere.exe was not found at $vswhere. Install Visual Studio Build Tools with Desktop C++ support."
    }

    $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsInstallPath) {
        throw "Visual Studio Build Tools with the C++ workload were not found."
    }

    $vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found at $vsDevCmd"
    }

    Write-Step "Importing MSVC build environment from $vsDevCmd"
    $environmentLines = cmd.exe /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    foreach ($line in $environmentLines) {
        if ($line -match "^(.*?)=(.*)$") {
            if ($matches[1].StartsWith("=")) {
                continue
            }
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC environment import completed, but cl.exe is still not available."
    }
}

function Configure-Build([string]$CMakeRoot, [string]$NinjaExe, [string]$VulkanSdkRoot) {
    Ensure-Directory $BuildDir

    $cmakeExe = Join-Path $CMakeRoot "bin\cmake.exe"
    $configureArgs = @(
        "-S", $Root,
        "-B", $BuildDir,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCMAKE_MAKE_PROGRAM=$NinjaExe",
        "-DVELIX_BUNDLED_CMAKE_DIR=$CMakeRoot",
        "-DVulkan_ROOT=$VulkanSdkRoot",
        "-DCMAKE_INSTALL_PREFIX=$OutputDir"
    )

    Write-Step "Configuring project"
    & $cmakeExe @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed"
    }
}

function Build-Project([string]$CMakeRoot) {
    $cmakeExe = Join-Path $CMakeRoot "bin\cmake.exe"

    Write-Step "Building project"
    & $cmakeExe --build $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
}

function Install-Project([string]$CMakeRoot) {
    $cmakeExe = Join-Path $CMakeRoot "bin\cmake.exe"
    Ensure-Directory $OutputDir

    Write-Step "Installing build output to $OutputDir"
    & $cmakeExe --install $BuildDir --prefix $OutputDir
    if ($LASTEXITCODE -ne 0) {
        throw "Install failed"
    }
}

function Parse-Mode([string[]]$Arguments) {
    $selectedMode = $null

    foreach ($argument in $Arguments) {
        switch ($argument) {
            "--build-all" {
                if ($selectedMode) { throw "Only one build mode may be specified." }
                $selectedMode = "build-all"
            }
            "--build-clean" {
                if ($selectedMode) { throw "Only one build mode may be specified." }
                $selectedMode = "build-clean"
            }
            "--build" {
                if ($selectedMode) { throw "Only one build mode may be specified." }
                $selectedMode = "build"
            }
            "--help" {
                Show-Usage
                exit 0
            }
            "-h" {
                Show-Usage
                exit 0
            }
            default {
                throw "Unknown argument: $argument"
            }
        }
    }

    if (-not $selectedMode) {
        return "build"
    }

    return $selectedMode
}

$Mode = Parse-Mode $args

$forceToolRefresh = $Mode -eq "build-all"
$cleanBuild = $Mode -eq "build-all" -or $Mode -eq "build-clean"

Ensure-Directory $DownloadsDir

if ($forceToolRefresh) {
    Write-Step "Refreshing local Windows tool cache"
    Remove-DirectoryIfExists $ToolCacheDir
    Ensure-Directory $DownloadsDir
}

if ($cleanBuild) {
    Write-Step "Removing previous build artifacts"
    Remove-DirectoryIfExists $BuildDir
    Remove-DirectoryIfExists $OutputDir
}

$cmakeRoot = Ensure-CMake -ForceRefresh:$forceToolRefresh
$ninjaExe = Ensure-Ninja -ForceRefresh:$forceToolRefresh
$vulkanSdkRoot = Ensure-VulkanSdk -ForceRefresh:$forceToolRefresh

Import-VisualStudioBuildEnvironment

$env:PATH = "$($CMakeRoot)\bin;$NinjaDir;$vulkanSdkRoot\Bin;$env:PATH"
$env:VULKAN_SDK = $vulkanSdkRoot

$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
$needsConfigure = $cleanBuild -or (-not (Test-Path -LiteralPath $cacheFile))

if ($needsConfigure) {
    Configure-Build -CMakeRoot $cmakeRoot -NinjaExe $ninjaExe -VulkanSdkRoot $vulkanSdkRoot
} else {
    Write-Step "Reusing existing CMake build directory at $BuildDir"
}

Build-Project -CMakeRoot $cmakeRoot
Install-Project -CMakeRoot $cmakeRoot

Write-Step "Build complete"
Write-Host "Build directory: $BuildDir"
Write-Host "Installed output: $OutputDir"
Write-Host "Local tools cache: $ToolCacheDir"
