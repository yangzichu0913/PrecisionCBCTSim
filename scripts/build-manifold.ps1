<#
.SYNOPSIS
    下载并编译 Manifold（Apache-2.0 的流形 CSG 库），安装到 -InstallDir。

.DESCRIPTION
    只用来做「多部件布尔并集」与流形性校验：几何修复的最后一环。
    静态库（BUILD_SHARED_LIBS=OFF），发布时无需额外 DLL。
    需要 C++17 编译器与 CMake >= 3.20。

.EXAMPLE
    .\scripts\build-manifold.ps1
    .\scripts\build-manifold.ps1 -Configs Release
#>
[CmdletBinding()]
param(
    [string]$Version = 'v3.5.4',
    [string]$Root = 'D:\deps',
    [ValidateSet('Release', 'Debug')]
    [string[]]$Configs = @('Release', 'Debug'),
    [string]$Generator = 'Visual Studio 17 2022'
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok([string]$text) { Write-Host "    [OK]  $text" -ForegroundColor Green }
function Write-Bad([string]$text) { Write-Host "    [FAIL] $text" -ForegroundColor Red }

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmake) { $cmake.Source } elseif (Test-Path "$env:APPDATA\Python\Python313\Scripts\cmake.exe") {
    "$env:APPDATA\Python\Python313\Scripts\cmake.exe"
}
else { $null }
if (-not $cmakePath) {
    Write-Bad '未找到 cmake（>= 3.20）'
    exit 2
}

$safeVersion = $Version -replace '[^0-9A-Za-z\.]', ''
$sourceDir = Join-Path $Root "manifold-$safeVersion\src"
$buildDir = Join-Path $Root "manifold-$safeVersion\build"
$installDir = Join-Path $Root "manifold-$safeVersion\install"
$archive = Join-Path $env:TEMP "manifold-$safeVersion.tar.gz"

Write-Step "准备源码 $Version"
if (-not (Test-Path (Join-Path $sourceDir 'CMakeLists.txt'))) {
    if (-not (Test-Path $archive)) {
        Write-Host "    下载 https://github.com/elalish/manifold/archive/$Version.tar.gz"
        & curl.exe -L --fail --silent --show-error -o $archive "https://github.com/elalish/manifold/archive/$Version.tar.gz"
        if ($LASTEXITCODE -ne 0) { Write-Bad '下载失败'; exit 2 }
    }
    New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
    & tar.exe -xzf $archive -C $sourceDir --strip-components=1
    if ($LASTEXITCODE -ne 0) { Write-Bad '解压失败'; exit 2 }
}
Write-Ok $sourceDir

Write-Step '配置（静态库、无 TBB、无 2D CrossSection、无测试/绑定）'
# MANIFOLD_CROSS_SECTION 只控制 2D CrossSection，会额外引入 Clipper2；
# 本工程只用 3D 布尔并集，关掉它可以少一个子依赖（3D 布尔在核心库里）。
& $cmakePath -S $sourceDir -B $buildDir -G $Generator -A x64 `
    -DCMAKE_INSTALL_PREFIX="$installDir" `
    -DBUILD_SHARED_LIBS=OFF `
    -DCMAKE_DEBUG_POSTFIX=d `
    -DMANIFOLD_PAR=OFF `
    -DMANIFOLD_TEST=OFF `
    -DMANIFOLD_EXPORT=OFF `
    -DMANIFOLD_CROSS_SECTION=OFF `
    -DMANIFOLD_CBIND=OFF `
    -DMANIFOLD_PYBIND=OFF `
    -DMANIFOLD_JSBIND=OFF `
    -DMANIFOLD_ASSERT=OFF
if ($LASTEXITCODE -ne 0) { Write-Bad 'CMake 配置失败'; exit 3 }
Write-Ok '配置完成'

foreach ($config in $Configs) {
    Write-Step "编译并安装 $config"
    & $cmakePath --build $buildDir --config $config --parallel
    if ($LASTEXITCODE -ne 0) { Write-Bad "编译 $config 失败"; exit 4 }
    & $cmakePath --install $buildDir --config $config
    if ($LASTEXITCODE -ne 0) { Write-Bad "安装 $config 失败"; exit 5 }
    Write-Ok $config
}

Write-Host ''
Write-Host "完成：$installDir" -ForegroundColor Green
$configFile = Get-ChildItem $installDir -Recurse -Filter 'manifoldConfig.cmake' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($configFile) {
    Write-Host "CMake 配置：$($configFile.DirectoryName)" -ForegroundColor Gray
}
else {
    Write-Host '提示：未找到 manifoldConfig.cmake，构建时请用 -DManifold_DIR 或 -DCMAKE_PREFIX_PATH 指向安装目录' -ForegroundColor Yellow
}
