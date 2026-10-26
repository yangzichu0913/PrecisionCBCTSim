<#
.SYNOPSIS
    把构建好的 PrecisionCBCTSim 连同 Qt / VTK / MSVC 运行库整理成一个可以拷走直接运行的目录。

.DESCRIPTION
    步骤：
      1. 确认 build 目录里已有目标 exe；
      2. 复制 exe 到 -OutDir；
      3. 用 windeployqt 收集 Qt 插件与 DLL；
      4. 从 VTK 安装目录复制运行期 DLL；
      5. 用 `PrecisionCBCTSim.exe --version` 做一次冒烟验证（退出码 0 即视为可用）。

.EXAMPLE
    .\scripts\deploy.ps1 -QtDir D:\Qt\6.8.3\msvc2022_64 -VtkDir D:\deps\VTK-9.3.1\install
#>
[CmdletBinding()]
param(
    [string]$QtDir = $env:QT_ROOT,
    [string]$VtkDir = $env:VTK_DIR,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$BuildDir,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot ("dist\$Configuration") }

function Write-Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok([string]$text) { Write-Host "    [OK]  $text" -ForegroundColor Green }
function Write-Bad([string]$text) { Write-Host "    [FAIL] $text" -ForegroundColor Red }

# ------------------------------------------------------------ 前置检查
Write-Step '检查构建产物'
$exeName = 'PrecisionCBCTSim.exe'
$exePath = Join-Path $BuildDir "bin\$Configuration\$exeName"
if (-not (Test-Path $exePath)) {
    Write-Bad "未找到 $exePath，请先运行 scripts\build.ps1 -Configuration $Configuration"
    exit 2
}
Write-Ok $exePath

if (-not $QtDir) {
    # 从已配置的构建缓存里猜 Qt 位置，省得用户再传一次
    $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path $cacheFile) {
        $line = Select-String -Path $cacheFile -Pattern '^Qt6_DIR:' | Select-Object -First 1
        if ($line) {
            $qtDirGuess = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $line.Line.Split('=')[1]))
            if (Test-Path (Join-Path $qtDirGuess 'bin\windeployqt.exe')) { $QtDir = $qtDirGuess }
        }
    }
}
if (-not $QtDir) {
    Write-Bad '未指定 -QtDir（或设置环境变量 QT_ROOT），无法收集 Qt 运行库'
    exit 2
}
$windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    Write-Bad "未找到 windeployqt：$windeployqt"
    exit 2
}

if (-not $VtkDir) {
    $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path $cacheFile) {
        $line = Select-String -Path $cacheFile -Pattern '^VTK_DIR:' | Select-Object -First 1
        if ($line) {
            # VTK_DIR 形如 <install>\lib\cmake\vtk-9.3，上溯三级即安装根目录
            $VtkDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $line.Line.Split('=')[1]))
        }
    }
}
if (-not $VtkDir -or -not (Test-Path (Join-Path $VtkDir 'bin'))) {
    Write-Bad '未指定 -VtkDir（或设置环境变量 VTK_DIR），找不到 VTK 运行库目录'
    exit 2
}
Write-Ok "Qt  : $QtDir"
Write-Ok "VTK : $VtkDir"

# ------------------------------------------------------------ 复制文件
Write-Step "准备输出目录 $OutDir"
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
Copy-Item $exePath (Join-Path $OutDir $exeName)

Write-Step 'windeployqt 收集 Qt 运行库'
# windeployqt 会把「无翻译文件 / 无 dxcompiler」之类的警告写到 stderr。
# Windows PowerShell 5 会把这些 stderr 行当成终止性错误（$ErrorActionPreference=Stop），
# 因此这一段临时改成 Continue 并丢弃 stderr。
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $windeployqt --no-translations --compiler-runtime --no-system-d3d-compiler `
    "--$($Configuration.ToLower())" (Join-Path $OutDir $exeName) 2>$null | Out-Null
$deployExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($deployExit -ne 0) {
    Write-Bad "windeployqt 失败（退出码 $deployExit）"
    exit 3
}
Write-Ok 'Qt 运行库已就位'

# windeployqt 的 --compiler-runtime 依赖 VCINSTALLDIR，在普通 PowerShell 里通常没设置，
# 这时需要自己从 VC Redist 目录补 MSVC 运行库。
if (-not (Test-Path (Join-Path $OutDir 'msvcp140.dll'))) {
    Write-Step '补 MSVC 运行库'
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $redistDir = $null
    if (Test-Path $vswherePath) {
        $vsPath = & $vswherePath -latest -products * -property installationPath | Select-Object -First 1
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'VC\Redist\MSVC'
            if (Test-Path $candidate) {
                $redistDir = Get-ChildItem $candidate -Directory |
                    Sort-Object Name -Descending |
                    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
                    Where-Object { Test-Path $_ } |
                    Select-Object -First 1
            }
        }
    }
    if ($redistDir) {
        Copy-Item (Join-Path $redistDir '*.dll') $OutDir -Force
        Write-Ok "已复制 VC 运行库：$redistDir"
    }
    else {
        Write-Host '    [WARN] 未能自动定位 VC 运行库，目标机需安装 VC++ 2015-2022 Redistributable' -ForegroundColor Yellow
    }
}

Write-Step '复制 VTK 运行库'
# 只拷 DLL，Debug 配置还要带上带 d 后缀的同名库
$vtkBin = Join-Path $VtkDir 'bin'
$vtkDlls = Get-ChildItem $vtkBin -Filter '*.dll' -File
$copied = 0
foreach ($dll in $vtkDlls) {
    Copy-Item $dll.FullName (Join-Path $OutDir $dll.Name)
    ++$copied
}
Write-Ok "复制 $copied 个 VTK/依赖 DLL"

# CUDA 运行时（GPU 加速用）：cudart64_*.dll，从 CUDA_PATH\bin 拷
Write-Step '复制 CUDA 运行时'
$cudaPath = $env:CUDA_PATH
if ($cudaPath -and (Test-Path (Join-Path $cudaPath 'bin'))) {
    $cudart = Get-ChildItem (Join-Path $cudaPath 'bin') -Filter 'cudart64_*.dll' -File -ErrorAction SilentlyContinue
    if ($cudart) {
        foreach ($dll in $cudart) { Copy-Item $dll.FullName (Join-Path $OutDir $dll.Name) -Force }
        Write-Ok "已复制 $($cudart.Count) 个 CUDA 运行时 DLL"
    }
    else {
        Write-Host '    [WARN] 未找到 cudart64_*.dll；GPU 加速在目标机不可用（会自动退回 CPU）' -ForegroundColor Yellow
    }
}
else {
    Write-Host '    [WARN] 未设置 CUDA_PATH；跳过 CUDA 运行时（GPU 加速在目标机不可用）' -ForegroundColor Yellow
}

$license = Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt'
if (Test-Path $license) { Copy-Item $license $OutDir }

# 示例模型：装好就能打开几个零件试手（README 里承诺过）
$samplesDir = Join-Path $repoRoot 'samples'
if (Test-Path $samplesDir) {
    $samplesOut = Join-Path $OutDir 'samples'
    New-Item -ItemType Directory -Force -Path $samplesOut | Out-Null
    Copy-Item (Join-Path $samplesDir '*') $samplesOut -Force
    Write-Host '    [OK]  已复制示例模型（samples）' -ForegroundColor Green
}

# ------------------------------------------------------------ 冒烟验证
Write-Step '冒烟验证（--version）'
$env:PATH = "$OutDir;$env:PATH"
# 必须重定向输出：GUI 子系统程序在没有控制台/输出句柄时，Qt 打印版本号那一步会挂住
$smokeOut = Join-Path $env:TEMP 'pcsim_smoke_out.txt'
$smokeErr = Join-Path $env:TEMP 'pcsim_smoke_err.txt'
$process = Start-Process -FilePath (Join-Path $OutDir $exeName) -ArgumentList '--version' -Wait -PassThru -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr
Remove-Item $smokeOut, $smokeErr -Force -ErrorAction SilentlyContinue
if ($process.ExitCode -ne 0) {
    Write-Bad "--version 退出码 $($process.ExitCode)（通常是缺少 DLL）"
    exit 4
}
Write-Ok '程序可以启动'

$totalMb = [math]::Round(((Get-ChildItem $OutDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host ''
Write-Host "完成：$OutDir（$totalMb MB）" -ForegroundColor Green
Write-Host '如需安装包，用 Inno Setup 编译 installer\PrecisionCBCTSim.iss' -ForegroundColor Gray
