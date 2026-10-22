$ErrorActionPreference = 'Stop'
$base = Split-Path $PSScriptRoot -Parent
$path = Join-Path $base 'src\engine\Win11Usb.Engine.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Script parse failed' }
foreach ($name in @('Get-InstallImagePlan', 'Confirm-InstallImageStep')) {
    $fn = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name}, $false)
    . ([scriptblock]::Create($fn.Extent.Text))
}
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Expect-Error([scriptblock]$Action, [string]$Expected) {
    $caught = $null
    try { & $Action } catch { $caught = $_.Exception.Message }
    Check ($null -ne $caught -and $caught.Contains($Expected)) "Expected error '$Expected', got '$caught'"
}
function File([string]$Relative, [long]$Length) {
    $fullName = Join-Path $script:fixtureSource $Relative
    return [pscustomobject]@{FullName=$fullName; Name=[IO.Path]::GetFileName($fullName); Length=$Length}
}
$script:fixtureSource = Join-Path $PSScriptRoot 'fake-iso\'
$source = $script:fixtureSource
$bootFiles = @((File 'setup.exe' 1000), (File 'efi\boot\bootx64.efi' 2000), (File 'sources\boot.wim' 3000))
foreach ($length in @([long]4054128535, [long]4294967294, [long]4294967295)) {
    $plan = Get-InstallImagePlan $source ($bootFiles + @(File 'sources\install.wim' $length))
    Check ($plan.HasWim -and -not $plan.RequiresSplit) "WIM <= limit unnecessarily split: $length"
    Check ($plan.ImageFiles[0].Length -eq $length) 'Size changed in plan'
}
$largePlan = Get-InstallImagePlan $source ($bootFiles + @(File 'sources\install.wim' 4294967296))
Check $largePlan.RequiresSplit 'Limit + 1 must split'
foreach ($relative in @('sources\install.esd', 'sources\install.swm')) {
    $plan = Get-InstallImagePlan $source ($bootFiles + @(File $relative 4054128535))
    Check (-not $plan.HasWim -and -not $plan.RequiresSplit -and $plan.ImageFiles.Count -eq 1) "Existing image unsupported: $relative"
    Expect-Error { Get-InstallImagePlan $source ($bootFiles + @(File $relative 4294967296)) } '超过 FAT32'
}
$existingSwm = Get-InstallImagePlan $source ($bootFiles + @((File 'sources\install.swm' 3800000000), (File 'sources\install2.swm' 1000000000)))
Check ($existingSwm.ImageFiles.Count -eq 2) 'SWM parts not listed'
Expect-Error { Get-InstallImagePlan $source $bootFiles } 'ISO 缺少安装映像'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @((File 'sources\install.wim' 1000), (File 'sources\install.swm' 1000))) } '同时包含 WIM 和 SWM'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @(File 'sources\install.wim' 0)) } 'install.wim 为空'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @((File 'sources\install.wim' 4294967296), (File 'other.bin' 4294967296))) } '超过 FAT32'

# Capture console and input without displaying or creating any real device UI.
$script:messages = New-Object 'Collections.Generic.List[string]'
$script:prompts = New-Object 'Collections.Generic.List[string]'
$script:answers = New-Object 'Collections.Generic.Queue[string]'
function Write-Host { param($Object, $ForegroundColor) $script:messages.Add([string]$Object) }
function Read-Host { param($Prompt) $script:prompts.Add([string]$Prompt); if (-not $script:answers.Count) { throw 'Unexpected additional prompt' }; return $script:answers.Dequeue() }
$smallPlan = Get-InstallImagePlan $source ($bootFiles + @(File 'sources\install.wim' 4054128535))
$script:answers.Enqueue('')
Confirm-InstallImageStep $smallPlan
Check ($script:prompts.Count -eq 1 -and $script:prompts[0].Contains('跳过拆分并开始复制')) 'Small WIM confirmation misleading'
Check (($script:messages -join "`n").Contains('4,054,128,535') -and ($script:messages -join "`n").Contains('4,294,967,295')) 'Exact file size / limit missing'
$script:prompts.Clear(); $script:answers.Enqueue('')
Confirm-InstallImageStep $largePlan
Check ($script:prompts.Count -eq 1 -and $script:prompts[0].Contains('执行本机拆分')) 'Large WIM confirmation misleading'
foreach ($plan in @($smallPlan, $largePlan)) {
    $script:answers.Enqueue('q')
    Expect-Error { Confirm-InstallImageStep $plan } '已退出'
}
$script:prompts.Clear(); $script:answers.Enqueue('invalid'); $script:answers.Enqueue('')
Confirm-InstallImageStep $smallPlan
Check ($script:prompts.Count -eq 2) 'Unexpected input did not retry'

# Execute ONLY the extracted step 3-5 code, with native commands and storage mocked.
# Start-UsbSteps and the script entry point are never invoked; no disks are formatted.
$startFunction = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Start-UsbSteps'}, $false)
$startText = $startFunction.Extent.Text
$sectionStart = $startText.IndexOf('    Confirm-InstallImageStep $imagePlan')
$sectionEnd = $startText.IndexOf('    Write-Host "`n[6/6]')
Check ($sectionStart -ge 0 -and $sectionEnd -gt $sectionStart) 'Integration section not found'
$section = [scriptblock]::Create($startText.Substring($sectionStart, $sectionEnd - $sectionStart))
function Assert-SourceMediaIdentity { }
$script:fixtureFs = 'FAT32'
function Get-Target { return [pscustomobject]@{Number=999} }
function Get-Partition { param($DiskNumber, $PartitionNumber); return [pscustomobject]@{DiskNumber=$DiskNumber; PartitionNumber=$PartitionNumber} }
function Get-Volume { param([Parameter(ValueFromPipeline=$true)]$InputObject); process { return [pscustomobject]@{DriveLetter='Z'; FileSystem=$script:fixtureFs} } }
function New-Item { param($ItemType, $Path); if ($Path -ne $script:fakeStaging) { throw 'Unexpected directory creation' } }
function Get-ChildItem { param($LiteralPath, [switch]$File, $Filter); if ($LiteralPath -ne $script:fakeStaging) { throw 'Unexpected enumeration' }; return $script:fakeSwm }
function Test-Path { param($LiteralPath); return $LiteralPath -eq (Join-Path $script:fakeStaging 'install.swm') }
function Step { param($Number, $Title); $script:stepNumbers.Add($Number) }
function Run-Command {
    param($Program, [string[]]$Arguments, $Title, [object[]]$CopyFiles)
    $script:commands.Add([pscustomobject]@{Program=$Program; Arguments=$Arguments; Files=$CopyFiles})
    if ($Program -eq 'dism.exe') { return }
    if ($Program -ne 'robocopy.exe') { throw 'Unexpected native command' }
    foreach ($file in $CopyFiles) {
        if ($Arguments[0] -eq $script:fixtureSource) { $relative = $file.FullName.Substring($script:fixtureSource.Length) }
        else { $relative = $file.Name }
        $copiedPath = Join-Path $Arguments[1] $relative
        $script:copied[$copiedPath] = [long]$file.Length
    }
}
function Get-Item {
    param($LiteralPath, $ErrorAction)
    $script:verified.Add($LiteralPath)
    if (-not $script:copied.ContainsKey($LiteralPath)) { throw "Uncopied file requested: $LiteralPath" }
    $length = $script:copied[$LiteralPath]
    if ($script:corruptPath -eq $LiteralPath) { $length-- }
    return [pscustomobject]@{Length=$length}
}
function Run-Fixture([long]$WimLength, [string]$ImageName = 'install.wim', [bool]$Corrupt = $false, [bool]$Quit = $false, [bool]$InvalidSwm = $false) {
    $fileSystem = $script:fixtureFs
    $source = $script:fixtureSource
    $files = $bootFiles + @(File ('sources\' + $ImageName) $WimLength)
    $imagePlan = Get-InstallImagePlan $source $files
    $wim = $imagePlan.WimPath; $split = $imagePlan.RequiresSplit
    $staging = Join-Path $PSScriptRoot 'fake-staging'
    $script:fakeStaging = $staging
    $script:fakeSwm = @(
        [pscustomobject]@{Name='install.swm'; FullName=(Join-Path $staging 'install.swm'); Length=[long]3000000000},
        [pscustomobject]@{Name='install2.swm'; FullName=(Join-Path $staging 'install2.swm'); Length=[long]1500000000}
    )
    if ($InvalidSwm) { $script:fakeSwm[0].Length = [long]4294967296 }
    $sizeMB = 14000; $letter = 'Z'; $destination = Join-Path $PSScriptRoot 'fake-usb\'; $partNumber = 1
    $script:diskNumber = 999
    $script:commands = New-Object 'Collections.Generic.List[object]'
    $script:stepNumbers = New-Object 'Collections.Generic.List[int]'
    $script:verified = New-Object 'Collections.Generic.List[string]'
    $script:copied = @{}
    $script:corruptPath = if ($Corrupt) { Join-Path $destination 'sources\install.wim' } else { '' }
    $script:answers.Enqueue($(if ($Quit) { 'Q' } else { '' }))
    if ($Quit) { Expect-Error { . $section } '已退出'; Check ($script:commands.Count -eq 0) 'Q still executed native commands'; return }
    if ($InvalidSwm) { Expect-Error { . $section } '拆分结果不完整'; Check ($script:commands.Count -eq 1) 'Invalid SWM was copied'; return }
    if ($Corrupt) { Expect-Error { . $section } '复制大小不一致'; return }
    . $section
    $copies = @($script:commands | Where-Object Program -eq 'robocopy.exe')
    if ($split) {
        Check ($script:commands.Count -eq 3 -and $script:commands[0].Program -eq 'dism.exe' -and $copies.Count -eq 2) 'Split route commands incorrect'
        Check ($copies[0].Arguments -contains '/XF' -and $copies[0].Arguments -contains 'install.wim') 'Original WIM not excluded'
        Check (@($copies[0].Files | Where-Object FullName -eq $wim).Count -eq 0) 'Original WIM included in split progress inventory'
        Check (-not $script:verified.Contains((Join-Path $destination 'sources\install.wim'))) 'Split route tried to verify original WIM'
        Check ($script:verified.Contains((Join-Path $destination 'sources\install.swm')) -and $script:verified.Contains((Join-Path $destination 'sources\install2.swm'))) 'SWM files not verified'
        Check ($script:stepNumbers -contains 4) 'Split route lost copy confirmation'
    } else {
        Check ($script:commands.Count -eq 1 -and $copies.Count -eq 1) 'Direct route unexpectedly ran DISM / extra copy'
        Check (-not ($copies[0].Arguments -contains '/XF')) 'Direct image excluded from copy'
        $expectedPath = Join-Path $destination ('sources\' + $ImageName)
        Check ($script:verified.Contains($expectedPath)) 'Direct installation image not verified'
        Check (-not ($script:stepNumbers -contains 4)) 'Direct route asks to confirm copy twice'
    }
}
foreach ($length in @([long]4054128535, [long]4294967295, [long]4294967296)) { Run-Fixture $length }
Run-Fixture 1000000000 'install.esd'
Run-Fixture 1000000000 'install.swm'
Run-Fixture 4054128535 'install.wim' $true
Run-Fixture 4054128535 'install.wim' $false $true
Run-Fixture 4294967296 'install.wim' $false $true
Run-Fixture 4294967296 'install.wim' $false $false $true


$script:fixtureFs='NTFS'
foreach ($length in @([long]4054128535,[long]4294967295,[long]4294967296)) { Run-Fixture $length }
'PASS: current engine image boundaries, direct/split copy, NTFS same split rule, corruption/oversized SWM rejection, Enter/Q behavior.'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @((File 'sources\install.swm' 1000),(File 'sources\install3.swm' 1000))) } '不连续'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @((File 'sources\install.wim' 1000),(File 'sources\install.esd' 1000))) } '多种安装映像'
Expect-Error { Get-InstallImagePlan $source ($bootFiles + @(File 'sources\install.esd' 0)) } '安装映像为空'
$tenParts=@(File 'sources\install.swm' 1000)+@(2..10 | ForEach-Object {File ('sources\install'+$_+'.swm') 1000})
Check ((Get-InstallImagePlan $source ($bootFiles+$tenParts)).ImageFiles.Count -eq 10) 'Numeric SWM sequence >= 10 was rejected'
'PASS: ambiguous image sets, empty ESD, missing SWM parts rejected; ten-part SWM sequence supported.'
