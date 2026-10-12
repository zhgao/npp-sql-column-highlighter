# build.ps1 — 直接用 cl.exe / link.exe 编译插件 DLL（不需要 vcxproj）。
#
# 用法：
#   本地：在 "x64 Native Tools Command Prompt for VS" 里运行
#       powershell -ExecutionPolicy Bypass -File build.ps1 -Arch x64
#   CI：  GitHub Actions 会先加载 msvc-dev-cmd，再调这个脚本。
param(
    [ValidateSet("x86", "x64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root "src"
$dist = Join-Path $root "dist\$Arch"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# 1. 编译版本资源
rc.exe /nologo /fo "$dist\version.res" "$src\version.rc"

# 2. 编译 + 链接成 DLL（/MT 静态链接 CRT：用户安装插件零依赖）
cl.exe /nologo /LD /MT /utf-8 /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS `
    /I "$src" `
    "$src\PluginMain.cpp" "$src\Highlighter.cpp" "$dist\version.res" `
    /link /NOLOGO /DLL /OUT:"$dist\SqlColumnHighlighter.dll" user32.lib

Write-Host ""
Write-Host "Built: $dist\SqlColumnHighlighter.dll"
