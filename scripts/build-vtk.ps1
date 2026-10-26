<#
.SYNOPSIS
    下载并编译 VTK 9.3.1（启用 Qt 支持），安装到本地依赖目录。

.DESCRIPTION
    本项目需要「带 Qt 支持的 VTK」，官方不提供 Windows 预编译包，必须自己编译。
    本脚本自动化整套流程：下载源码 → CMake 配置 → 编译（默认 Debug + Release）→ 安装，
    结束后打印可直接传给 build.ps1 的 -VtkDir 参数。

    前置条件：MSVC v143（VS 2022 + “使用 C++ 的桌面开发”）、CMake >= 3.21、Qt 6（msvc2022_64）。

.EXAMPLE
    .\scripts\build-vtk.ps1

.EXAMPLE
    # 只编 Release（省一半时间），并指定安装位置
    .\scripts\build-vtk.ps1 -Configs Release -Root D:\deps\VTK-9.3.1

.EXAMPLE
    # 只看会执行什么，不真正下载/编译
    .\scripts\build-vtk.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [string]$Root = 'D:\deps\VTK-9.3.1',
    [string]$SourceDir,
    [string]$QtDir = $env:QT_ROOT,
    [string[]]$Configs = @('Release', 'Debug'),
    [string]$CMake,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$vtkVersion = '9.3.1'

function Write-Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok([string]$text) { Write-Host "    [OK]  $text" -ForegroundColor Green }
function Write-Bad([string]$text) { Write-Host "    [FAIL] $text" -ForegroundColor Red }
function Write-Info([string]$text) { Write-Host "          $text" -ForegroundColor DarkGray }

function Invoke-Step([string]$exe, [string[]]$arguments, [string]$what)
{
    Write-Info ("{0} {1}" -f $exe, ($arguments -join ' '))
    if ($DryRun) { return }
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$what 失败（退出码 $LASTEXITCODE）" }
}

# ------------------------------------------------------------ 前置检查
Write-Step '检查前置条件'
$problems = New-Object System.Collections.Generic.List[string]

if (-not $CMake) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $CMake = $cmd.Source }
    elseif (Test-Path "$env:APPDATA\Python\Python313\Scripts\cmake.exe") {
        $CMake = "$env:APPDATA\Python\Python313\Scripts\cmake.exe"
    }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -property installationPath
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { $CMake = $candidate }
        }
    }
}
if ($CMake -and (Test-Path $CMake)) { Write-Ok "CMake: $CMake" }
else { Write-Bad '未找到 cmake（需要 >= 3.21）'; $problems.Add('cmake') }

# MSVC v143：必须同时存在 cl.exe 与 lib\x64（只有头文件说明安装不完整）
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$toolset = $null
if (Test-Path $vswherePath) {
    $instance = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    # 注意：变量名不能叫 $root —— PowerShell 变量大小写不敏感，会覆盖 -Root 参数
    foreach ($vsInstall in $instance) {
        $msvcRoot = Join-Path $vsInstall 'VC\Tools\MSVC'
        if (-not (Test-Path $msvcRoot)) { continue }
        foreach ($dir in (Get-ChildItem $msvcRoot -Directory | Sort-Object Name -Descending)) {
            $cl = Join-Path $dir.FullName 'bin\Hostx64\x64\cl.exe'
            $lib = Join-Path $dir.FullName 'lib\x64'
            if ((Test-Path $cl) -and (Test-Path $lib) -and ([version]$dir.Name -ge [version]'14.30')) {
                $toolset = $dir.Name
                break
            }
        }
        if ($toolset) { break }
    }
}
if ($toolset) { Write-Ok "MSVC 工具集: $toolset" }
else {
    Write-Bad '未找到可用的 MSVC v143 工具集（需要 cl.exe + lib\x64，且版本 >= 14.30）。请在 Visual Studio Installer 中确认 "MSVC v143 - VS 2022 C++ x64/x86 生成工具" 已安装完成'
    $problems.Add('msvc-v143')
}

if (-not $QtDir) {
    $qtConfig = Get-ChildItem 'D:\Qt', 'C:\Qt' -Recurse -Depth 5 -Filter 'Qt6Config.cmake' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'msvc2022_64' } |
        Select-Object -First 1 -ExpandProperty FullName
    if ($qtConfig) {
        $QtDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtConfig)))
    }
}
if ($QtDir -and (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) { Write-Ok "Qt: $QtDir" }
else { Write-Bad '未找到 Qt 6 (msvc2022_64)，用 -QtDir 指定'; $problems.Add('qt6') }

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host "缺少依赖：$($problems -join ', ')。补齐后重新运行本脚本。" -ForegroundColor Yellow
    exit 1
}

# ---------------------------------------------------------------- 源码
if (-not [System.IO.Path]::IsPathRooted($Root)) {
    Write-Bad "-Root 必须是绝对路径（当前值：$Root）"
    exit 1
}
$src = $SourceDir
if (-not $src) { $src = Join-Path $Root "src\VTK-$vtkVersion" }

if (-not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
    $archive = Join-Path $Root "VTK-$vtkVersion.tar.gz"
    New-Item -ItemType Directory -Path (Split-Path -Parent $src) -Force | Out-Null
    if (-not (Test-Path $archive)) {
        Write-Step "下载 VTK $vtkVersion 官方源码包（约 100 MB）"
        $url = "https://www.vtk.org/files/release/9.3/VTK-$vtkVersion.tar.gz"
        Invoke-Step 'curl.exe' @('-L', '--fail', '--retry', '3', '-o', $archive, $url) '下载 VTK 源码'
    }
    if (-not $DryRun) {
        Write-Step '解压源码（tar.exe，Windows 10 1803+ 自带）'
        $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
        if (-not (Test-Path $tar)) { throw "未找到 $tar，无法解压 .tar.gz" }
        & $tar -xzf $archive -C (Join-Path $Root 'src')
        if ($LASTEXITCODE -ne 0) { throw "解压失败（退出码 $LASTEXITCODE）" }
        if (-not (Test-Path (Join-Path $src 'CMakeLists.txt'))) {
            throw "解压后未找到 $src\CMakeLists.txt，请检查压缩包"
        }
    }
}
Write-Ok "源码: $src"

# ------------------------------------------------------------ 配置/编译
$buildDir = Join-Path $Root 'build'
$installDir = Join-Path $Root 'install'

Write-Step 'CMake 配置（启用 GUISupportQt 与后续阶段需要的 IO/Filter 模块）'
$configureArgs = @(
    '-S', $src,
    '-B', $buildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DCMAKE_INSTALL_PREFIX=$installDir",
    '-DCMAKE_CXX_STANDARD=17',
    '-DVTK_GROUP_ENABLE_Rendering=WANT',
    '-DVTK_GROUP_ENABLE_StandAlone=WANT',
    '-DVTK_GROUP_ENABLE_Imaging=WANT',
    '-DVTK_MODULE_ENABLE_VTK_GUISupportQt=WANT',
    '-DVTK_MODULE_ENABLE_VTK_IOGeometry=WANT',
    '-DVTK_MODULE_ENABLE_VTK_IOPLY=WANT',
    '-DVTK_MODULE_ENABLE_VTK_FiltersCore=WANT',
    '-DVTK_MODULE_ENABLE_VTK_ImagingCore=WANT',
    '-DVTK_BUILD_TESTING=OFF',
    '-DVTK_BUILD_EXAMPLES=OFF',
    '-DVTK_WRAP_PYTHON=OFF',
    '-DVTK_DATA_EXECUTABLE=OFF'
)
Invoke-Step $CMake $configureArgs 'CMake 配置'

if (-not $DryRun) {
    Write-Step "编译（$($Configs -join ' + ')，首次约 10-30 分钟）"
    foreach ($config in $Configs) {
        Invoke-Step $CMake @('--build', $buildDir, '--config', $config, '--parallel') "编译 $config"
        Write-Step "安装 $config 配置"
        Invoke-Step $CMake @('--install', $buildDir, '--config', $config) "安装 $config"
    }
}

Write-Host ''
Write-Ok "VTK 已安装到 $installDir"

# VTK 把 CMake 包配置装到 <prefix>/lib/cmake/vtk-<major.minor>，-VtkDir 需要指向该目录
$vtkCmakeDir = Get-ChildItem (Join-Path $installDir 'lib\cmake') -Directory -Filter 'vtk*' -ErrorAction SilentlyContinue |
    Where-Object { (Test-Path (Join-Path $_.FullName 'vtk-config.cmake')) -or (Test-Path (Join-Path $_.FullName 'VTKConfig.cmake')) } |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $vtkCmakeDir -and -not $DryRun) {
    Write-Bad "未在 $installDir\lib\cmake 下找到 vtk-config.cmake，请检查安装步骤输出"
    exit 1
}

Write-Host ''
Write-Host '接下来构建本项目：' -ForegroundColor Yellow
Write-Host ("  .\scripts\build.ps1 -QtDir `"$QtDir`" -VtkDir `"$vtkCmakeDir`" -Configuration Release") -ForegroundColor Yellow
