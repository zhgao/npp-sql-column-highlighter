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

# 3. 高亮逻辑端到端测试：伪造 Scintilla，直接驱动 analyse/highlight
#    （/FI 把 ::SendMessage 重定向到测试里的 FakeSendMessage）
Write-Host ""
Write-Host "=== Running highlighter tests ==="
cl.exe /nologo /EHsc /std:c++17 /utf-8 /D_CRT_SECURE_NO_WARNINGS `
    /I "$src" /FI"$root\tests\fake_sci.h" `
    "$root\tests\test_highlighter.cpp" "$src\Highlighter.cpp" `
    /link /NOLOGO /OUT:"$dist\test_highlighter.exe"
& "$dist\test_highlighter.exe"
if ($LASTEXITCODE -ne 0) { throw "highlighter tests failed" }

# 4. DLL 加载冒烟测试：验证导出的插件接口
Write-Host ""
Write-Host "=== Running DLL load test ==="
cl.exe /nologo /EHsc /std:c++17 /utf-8 /D_CRT_SECURE_NO_WARNINGS `
    "$root\tests\test_load.cpp" `
    /link /NOLOGO /OUT:"$dist\test_load.exe"
Push-Location $dist
& .\test_load.exe
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { throw "DLL load test failed" }

Write-Host ""
Write-Host "All tests passed."
