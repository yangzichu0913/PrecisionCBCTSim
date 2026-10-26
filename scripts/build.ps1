<#
.SYNOPSIS
    检查依赖并配置 / 构建 PrecisionCBCTSim（Windows + MSVC 2022 + Qt 6 + VTK 9）。

.DESCRIPTION
    依次检查 CMake、MSVC v143 工具链、Qt 6、VTK 9，然后执行 CMake 配置与构建。
    依赖位置可以显式传参，也可以来自环境变量：QT_ROOT、VCPKG_ROOT、VTK_DIR。

.EXAMPLE
    # 使用 vcpkg（VTK 与 Qt 都来自 vcpkg）构建 Debug
    .\scripts\build.ps1 -VcpkgRoot C:\vcpkg

.EXAMPLE
    # 使用官方 Qt 安装 + 自编译 VTK + 自编译 Manifold
    .\scripts\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64 -VtkDir C:\vtk-build
#>
[CmdletBinding()]
param(
    [string]$QtDir = $env:QT_ROOT,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VtkDir = $env:VTK_DIR,
    [string]$ManifoldDir = $env:MANIFOLD_DIR,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    # 默认 Release：自编译 VTK 通常只装 Release，Debug 需另外编
    [string]$Configuration = 'Release',
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }

function Write-Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok([string]$text) { Write-Host "    [OK]  $text" -ForegroundColor Green }
function Write-Bad([string]$text) { Write-Host "    [FAIL] $text" -ForegroundColor Red }

$problems = New-Object System.Collections.Generic.List[string]

# ---------------------------------------------------------------- CMake
Write-Step '查找 CMake'
$cmake = $null
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCmd) {
    $cmake = $cmakeCmd.Source
}
elseif (Test-Path "$env:APPDATA\Python\Python313\Scripts\cmake.exe") {
    # pip install cmake 的默认位置
    $cmake = "$env:APPDATA\Python\Python313\Scripts\cmake.exe"
}
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { $cmake = $candidate }
        }
    }
}
if ($cmake) { Write-Ok $cmake }
else {
    Write-Bad '未找到 cmake（安装 CMake >= 3.21，或用 VS Installer 勾选 "C++ CMake tools for Windows"；用 pip 安装时需把 Scripts 目录加入 PATH）'
    $problems.Add('cmake')
}

# ------------------------------------------------------- MSVC v143 (C++)
Write-Step '查找 MSVC v143 编译器'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$clFound = $null
$clToolsetVersion = $null
if (Test-Path $vswherePath) {
    $instances = & $vswherePath -latest -products * -property installationPath
    foreach ($instance in $instances) {
        $msvcRoot = Join-Path $instance 'VC\Tools\MSVC'
        if (-not (Test-Path $msvcRoot)) { continue }
        foreach ($toolset in (Get-ChildItem $msvcRoot -Directory | Sort-Object Name -Descending)) {
            $candidate = Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'
            # lib\x64 也必须存在：只有头文件的半成品安装无法链接
            $libDir = Join-Path $toolset.FullName 'lib\x64'
            if ((Test-Path $candidate) -and (Test-Path $libDir)) {
                $clFound = $candidate
                $clToolsetVersion = $toolset.Name
                break
            }
        }
        if ($clFound) { break }
    }
}
# Qt 6 要求 MSVC >= 14.27（VS 2019 16.7），本项目按 v143（14.3x）验收
if ($clFound -and [version]$clToolsetVersion -ge [version]'14.30') {
    Write-Ok "$clFound (工具集 $clToolsetVersion)"
}
elseif ($clFound) {
    Write-Bad "只找到旧工具集 $clToolsetVersion（$clFound）；Qt 6 需要 MSVC v143。请在 Visual Studio Installer 安装 'MSVC v143 - VS 2022 C++ x64/x86 生成工具'"
    $problems.Add('msvc-v143')
}
else {
    Write-Bad '未找到 MSVC 编译器。请在 Visual Studio Installer 中安装工作负载 "使用 C++ 的桌面开发"（MSVC v143 + Windows SDK）'
    $problems.Add('msvc')
}

# ----------------------------------------------------------------- Qt 6
Write-Step '查找 Qt 6'
$qtConfig = $null
if ($QtDir) {
    $qtConfig = Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'
}
elseif ($VcpkgRoot) {
    # vcpkg 方式下 Qt 也由 vcpkg 提供（VTK 的 qt 特性会拉入 qtbase）
    $qtConfig = Join-Path $VcpkgRoot 'installed\x64-windows\share\Qt6\Qt6Config.cmake'
}
else {
    $searchRoots = @('C:\Qt', 'D:\Qt') | Where-Object { Test-Path $_ }
    foreach ($root in $searchRoots) {
        $qtConfig = Get-ChildItem $root -Recurse -Depth 5 -Filter 'Qt6Config.cmake' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'msvc2022_64' } |
            Select-Object -First 1 -ExpandProperty FullName
        if ($qtConfig) { break }
    }
}
if ($qtConfig -and (Test-Path $qtConfig)) {
    Write-Ok $qtConfig
    if ($QtDir -or -not $VcpkgRoot) {
        # Qt6Config.cmake 位于 <Qt>/lib/cmake/Qt6/Qt6Config.cmake
        $QtDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtConfig)))
    }
}
else {
    Write-Bad '未找到 Qt 6 (msvc2022_64)。用 -QtDir 指定 Qt 安装目录，或先按 README 的 "依赖安装" 安装 Qt'
    $problems.Add('qt6')
}

# ----------------------------------------------------------------- VTK 9
Write-Step '查找 VTK 9'
$vtkConfig = $null
# VTK 9 的包配置文件名是小写的 vtk-config.cmake（find_package 同样识别）
$vtkConfigNames = @('VTKConfig.cmake', 'vtk-config.cmake')
if ($VtkDir) {
    # 允许传 VTK 安装根目录（含 lib\cmake\vtk-9.x）或直接传 CMake 包目录
    foreach ($name in $vtkConfigNames) {
        $candidate = Join-Path $VtkDir $name
        if (Test-Path $candidate) { $vtkConfig = $candidate; break }
    }
    if (-not $vtkConfig) {
        foreach ($dir in (Get-ChildItem (Join-Path $VtkDir 'lib\cmake') -Directory -Filter 'vtk*' -ErrorAction SilentlyContinue)) {
            foreach ($name in $vtkConfigNames) {
                $candidate = Join-Path $dir.FullName $name
                if (Test-Path $candidate) { $vtkConfig = $candidate; break }
            }
            if ($vtkConfig) { break }
        }
    }
}
if (-not $vtkConfig -and $VcpkgRoot) {
    foreach ($name in $vtkConfigNames) {
        $candidate = Join-Path $VcpkgRoot "installed\x64-windows\share\vtk\$name"
        if (Test-Path $candidate) { $vtkConfig = $candidate; break }
    }
}
if ($vtkConfig -and (Test-Path $vtkConfig)) {
    Write-Ok $vtkConfig
    $VtkDir = Split-Path -Parent $vtkConfig
}
else {
    Write-Bad '未找到 VTKConfig.cmake。用 -VcpkgRoot 或 -VtkDir 指定 VTK 安装位置'
    $problems.Add('vtk9')
}

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host "缺少依赖：$($problems -join ', ')" -ForegroundColor Yellow
    Write-Host '请先按 README.md 的 "依赖安装" 一节准备环境，然后重新运行本脚本。' -ForegroundColor Yellow
    exit 1
}

# --------------------------------------------------------- Manifold (CSG)
# 多部件布尔并集 / 内腔差集 / 非流形接缝修复，静态库，不需要额外 DLL
Write-Step '查找 Manifold（Apache-2.0 CSG 库）'
$manifoldDir = $ManifoldDir
if (-not $manifoldDir) {
    # 优先用 install 目录（build 树里的 config 没有版本号，find_package 会报 unknown version）
    $manifoldConfig = Get-ChildItem -Path 'D:\deps' -Recurse -Filter 'manifoldConfig.cmake' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\install\*' } |
        Select-Object -First 1
    if (-not $manifoldConfig) {
        $manifoldConfig = Get-ChildItem -Path 'D:\deps' -Recurse -Filter 'manifoldConfig.cmake' -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if ($manifoldConfig) { $manifoldDir = $manifoldConfig.DirectoryName }
}
if ($manifoldDir -and (Test-Path $manifoldDir)) {
    Write-Ok $manifoldDir
}
else {
    Write-Bad '未找到 manifoldConfig.cmake；先运行 .\scripts\build-manifold.ps1 编译安装（只需一次）'
    $problems.Add('manifold')
}

# VTK 是分配置安装的：只装了 Release 时用 Debug 构建会链接失败。
# MSVC 下 Debug 库名为 vtkCommonCore-9.3d.lib（版本号后面加 d）
$vtkLibDir = Split-Path -Parent (Split-Path -Parent $VtkDir)
$vtkDebugLib = Get-ChildItem $vtkLibDir -Filter 'vtkCommonCore-*d.lib' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($Configuration -eq 'Debug' -and -not $vtkDebugLib) {
    Write-Host ''
    Write-Bad "VTK 没有 Debug 配置（$vtkLibDir 下找不到 vtkCommonCore-*d.lib）"
    Write-Host '    请二选一：' -ForegroundColor Yellow
    Write-Host '      1) 用 Release 构建本项目：-Configuration Release' -ForegroundColor Yellow
    Write-Host '      2) 补编 VTK Debug：.\scripts\build-vtk.ps1 -Configs Debug' -ForegroundColor Yellow
    exit 1
}

# ------------------------------------------------------------ configure
$configureArgs = @(
    '-S', $repoRoot,
    '-B', $BuildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64'
)
if ($QtDir) {
    $configureArgs += "-DCMAKE_PREFIX_PATH=$QtDir"
}
if ($VcpkgRoot) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')"
}
if ($VtkDir) {
    $configureArgs += "-DVTK_DIR=$VtkDir"
}
if ($manifoldDir) {
    $configureArgs += "-Dmanifold_DIR=$manifoldDir"
}

Write-Step "配置：cmake $($configureArgs -join ' ')"
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（退出码 $LASTEXITCODE）" }

Write-Step "构建：cmake --build $BuildDir --config $Configuration"
& $cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）" }

$exe = Join-Path $BuildDir "bin\$Configuration\PrecisionCBCTSim.exe"
if (-not (Test-Path $exe)) {
    $exe = Get-ChildItem (Join-Path $BuildDir 'bin') -Filter 'PrecisionCBCTSim.exe' -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
Write-Host ''
if ($exe) {
    Write-Ok "可执行文件：$($exe.FullName)"
    Write-Host '运行前请确保 Qt DLL 可见：把 Qt 的 bin 目录加入 PATH，或对 exe 执行 windeployqt。'
}
