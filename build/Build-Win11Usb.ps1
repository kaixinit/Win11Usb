#requires -Version 5.1
[CmdletBinding()]
param([string]$OutputName = 'Win11Usb.exe')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path -LiteralPath $compiler)) { throw '未找到 Windows .NET Framework C# 编译器（csc.exe）。' }

$source = Join-Path $root 'src\Launcher\Program.cs'
$nativeSources = @(Get-ChildItem -LiteralPath (Join-Path $root 'src\Launcher') -Filter '*.cs' -File | Select-Object -ExpandProperty FullName)
$ui = Join-Path $root 'src\Ui'
$manifest = Join-Path $root 'src\Launcher\app.manifest'
$engine = Join-Path $root 'src\engine\Win11Usb.Engine.ps1'
$worker = Join-Path $root 'src\Win11Usb.Worker.ps1'
$iconSource = Join-Path $root 'assets\usb-eject-source.png'
$icon = Join-Path $root 'assets\usb-eject.ico'
$iconBuilderSource = Join-Path $PSScriptRoot 'Build-Icon.cs'
$iconBuilder = Join-Path $root 'work\Build-Icon.exe'
$automationGac = Join-Path $env:WINDIR 'Microsoft.NET\assembly\GAC_MSIL\System.Management.Automation'
$automationAssembly = Get-ChildItem -LiteralPath $automationGac -Filter 'System.Management.Automation.dll' -Recurse -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$outDir = Join-Path $root 'dist'
if ($OutputName -notmatch '^[A-Za-z0-9_-]+\.exe$') { throw 'OutputName 必须是简单的 .exe 文件名。' }
$output = Join-Path $outDir $OutputName
foreach ($file in @($source,$manifest,$engine,$worker,$iconSource,$iconBuilderSource,(Join-Path $ui 'MainWindow.xaml'),(Join-Path $ui 'MessageDialog.xaml'),(Join-Path $ui 'FormatDialog.xaml'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "缺少构建文件：$file" }
}
if (-not $automationAssembly) { throw "未找到 .NET Framework GAC 中的 Windows PowerShell 5.1 组件：$automationGac" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $iconBuilder) | Out-Null

$iconArguments = @('/nologo','/target:exe','/optimize+',"/out:$iconBuilder",'/reference:System.Drawing.dll',$iconBuilderSource)
& $compiler @iconArguments
if ($LASTEXITCODE -ne 0) { throw "图标转换器编译失败，退出代码 $LASTEXITCODE。" }
& $iconBuilder $iconSource $icon
if ($LASTEXITCODE -ne 0) { throw "图标转换失败，退出代码 $LASTEXITCODE。" }

$arguments = @(
    '/nologo','/target:winexe','/platform:anycpu','/optimize+','/warn:4',
    "/out:$output",
    "/win32icon:$icon",
    "/win32manifest:$manifest",
    '/reference:System.Management.dll',
    '/reference:System.Xaml.dll',
    "/reference:$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\WPF\WindowsBase.dll",
    "/reference:$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\WPF\PresentationCore.dll",
    "/reference:$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\WPF\PresentationFramework.dll",
    "/reference:$automationAssembly",
    "/resource:$(Join-Path $ui 'MainWindow.xaml'),MainWindow.xaml",
    "/resource:$(Join-Path $ui 'MessageDialog.xaml'),MessageDialog.xaml",
    "/resource:$(Join-Path $ui 'FormatDialog.xaml'),FormatDialog.xaml",
    "/resource:$engine,Win11Usb.Engine.ps1",
    "/resource:$worker,Win11Usb.Worker.ps1",
    "/resource:$icon,Win11Usb.ico"
)
$arguments += $nativeSources
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "C# 编译失败，退出代码 $LASTEXITCODE。" }

$inspector = Join-Path $root 'work\Inspect-Resources.exe'
& $compiler '/nologo' '/target:exe' ("/out:$inspector") (Join-Path $PSScriptRoot 'Inspect-Resources.cs')
if ($LASTEXITCODE -ne 0) { throw '资源检查器编译失败。' }
& $inspector $output
if ($LASTEXITCODE -ne 0) { throw "单 EXE 内置资源检查失败，退出代码 $LASTEXITCODE。" }

$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
Write-Host "单 EXE 构建和内置资源检查通过：$output" -ForegroundColor Green
Write-Host "SHA256：$hash"
