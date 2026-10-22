$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$base = Split-Path $PSScriptRoot -Parent
$path = Join-Path $base 'src\engine\Win11Usb.Engine.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Parse failed' }
foreach ($name in @('Get-InstallImageBaseIdentity','Get-ReleaseHint','Get-ReleaseRules','Resolve-InstallImageRelease','Get-InstallImageIdentity','Show-InstallImageIdentity','Start-IsoInspection')) {
    $fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$false)
    . ([scriptblock]::Create($fn.Extent.Text))
}
function Check([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
function Get-WindowsImage { param($ImagePath,$Index,$ErrorAction); if ($script:failMetadata) { throw 'Mocked read failure' }; $script:readImage=$ImagePath; return $script:metadata }
$script:failMetadata=$false
$cases=@(
    @{Name='Windows 10 Enterprise LTSC 2019';Edition='EnterpriseS';Build=17763;Iso='cn_windows_10_enterprise_ltsc_2019_x64_dvd_9c09ff24.iso';Label='WIN10LTSC';Release='2019'},
    @{Name='Windows 10 Enterprise LTSC 2021';Edition='EnterpriseS';Build=19041;Iso='zh-cn_windows_10_enterprise_ltsc_2021_x64_dvd_033b7312.iso';Label='WIN10LTSC';Release='2021'},
    @{Name='Windows 10 Enterprise LTSC 2021';Edition='EnterpriseS';Build=19044;Iso='zh-cn_windows_10_enterprise_ltsc_2021_x64_dvd_033b7312.iso';Label='WIN10LTSC';Release='2021'},
    @{Name='Windows 11 Enterprise LTSC 2024';Edition='EnterpriseS';Build=26100;Iso='zh-cn_windows_11_enterprise_ltsc_2024_x64_dvd_cff9cd2d.iso';Label='WIN11LTSC';Release='2024'},
    @{Name='Windows 10 Education';Edition='Education';Build=19041;Iso='zh-cn_windows_10_business_editions_version_22h2_updated_oct_2025_x64_dvd.iso';Label='WIN10_22H2';Release='22H2'},
    @{Name='Windows 10 Education';Edition='Education';Build=19045;Iso='zh-cn_windows_10_business_editions_version_22h2_updated_sep_2025_x64_dvd.iso';Label='WIN10_22H2';Release='22H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='zh-cn_windows_11_business_editions_version_24h2_updated_nov_2025_x64_dvd_2f40d7fb.iso';Label='WIN11_24H2';Release='24H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='zh-cn_windows_11_business_editions_version_25h2_x64_dvd_22759158.iso';Label='WIN11_25H2';Release='25H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26200;Iso='zh-cn_windows_11_business_editions_version_25h2_x64_dvd_22759158.iso';Label='WIN11_25H2';Release='25H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26300;Iso='zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06.iso';Label='WIN11_26H2';Release='26H2'},
    @{Name='Windows 10 Enterprise';Edition='EnterpriseS';Build=17763;Iso='';Label='WIN10LTSC';Release='2019'},
    @{Name='Windows 10 Enterprise';Edition='EnterpriseSN';Build=19041;Iso='';Label='WIN10LTSC';Release='2021'},
    @{Name='Windows 11 IoT Enterprise';Edition='IoTEnterpriseS';Build=26100;Iso='';Label='WIN11LTSC';Release='2024'},
    @{Name='Windows 10 Education';Edition='Education';Build=19045;Iso='';Label='WIN10_22H2';Release='22H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26200;Iso='';Label='WIN11_25H2';Release='25H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=26300;Iso='';Label='WIN11_26H2';Release='26H2'},
    @{Name='Windows 11 Education';Edition='Education';Build=28000;Iso='windows_11_version_26h1.iso';Label='WIN11_26H1';Release='26H1'},
    @{Name='Windows 10 Education';Edition='Education';Build=19041;Iso='';Label='WIN10';Release=''},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='';Label='WIN11';Release=''},
    @{Name='Windows 11 Education';Edition='Education';Build=22621;Iso='';Label='WIN11';Release=''},
    @{Name='Windows 11 Education 25H2';Edition='Education';Build=26100;Iso='';Label='WIN11_25H2';Release='25H2'},
    @{Name='Windows 11 Education';Description='Version 24H2';Edition='Education';Build=26100;Iso='';Label='WIN11_24H2';Release='24H2'},
    @{Name='Windows 10 Enterprise';Description='Enterprise LTSC 2021';Edition='EnterpriseS';Build=19041;Iso='';Label='WIN10LTSC';Release='2021'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='windows_11_26h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 10 Enterprise LTSC 2021';Edition='EnterpriseS';Build=17763;Iso='';Label='WIN10LTSC';Release='';Status='Conflict'},
    @{Name='Windows 10 Enterprise LTSC 2019';Edition='EnterpriseS';Build=17763;Iso='windows_10_enterprise_ltsc_2021.iso';Label='WIN10LTSC';Release='';Status='Conflict'},
    @{Name='Windows 10 Education';Edition='Education';Build=19045;Iso='windows_11_25h2.iso';Label='WIN10';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=26200;Iso='windows_11_24h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 10 Education';Edition='Education';Build=19045;Iso='windows_10_21h2.iso';Label='WIN10';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=26300;Iso='windows_11_25h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 10 Education';Edition='Education';Build=19045;Iso='windows_10_enterprise_ltsc_2021.iso';Label='WIN10';Release='';Status='Conflict'},
    @{Name='Windows 11 Enterprise LTSC 2024';Edition='EnterpriseS';Build=26100;Iso='windows_11_24h2.iso';Label='WIN11LTSC';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='windows_11_24h2_25h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='windows_10_windows_11_24h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=27000;Iso='windows_11_27h2.iso';Label='WIN11';Release='';Status='Conflict'},
    @{Name='Windows 11 Education';Edition='Education';Build=26100;Iso='D:\windows_11_25h2\backup.iso';Label='WIN11';Release=''},
    @{Name='Windows Server 2025 Standard';Edition='ServerStandard';Build=26100;Iso='windows_11_24h2.iso';Label='WINSERVER';Release='';Type='Server';Product='ServerNT'},
    @{Name='Windows 10 Enterprise';Edition='EnterpriseS';Build=19041;Iso='windows_10_business_editions_version_22h2.iso';Label='WIN10LTSC';Release='';Status='Conflict'},
    @{Name='Windows 10 Enterprise';Edition='EnterpriseS';Build=19041;Iso='windows_10_enterprise_ltsc2021.iso';Label='WIN10LTSC';Release='2021'}
)
foreach ($case in $cases) {
    $script:metadata=[pscustomobject]@{ImageName=$case.Name;ImageDescription=$(if ($case.ContainsKey('Description')) { $case.Description } else { '' });EditionId=$case.Edition;MajorVersion=10;MinorVersion=0;Build=$case.Build;SPBuild=1;InstallationType=$(if ($case.ContainsKey('Type')) { $case.Type } else { 'Client' });ProductType=$(if ($case.ContainsKey('Product')) { $case.Product } else { 'WinNT' })}
    $identity=Get-InstallImageIdentity 'H:\sources\install.wim' $case.Iso
    Check ($identity.VolumeLabel -ceq $case.Label -and $identity.Release -ceq $case.Release) "Incorrect $($case.Name) / $($case.Build) / $($case.Iso): $($identity.VolumeLabel) / $($identity.Release)"
    Check ($identity.VolumeLabel.Length -le 11 -and $identity.VolumeLabel -cmatch '^[A-Z0-9_]+$') 'Invalid FAT32 label'
    if ($case.ContainsKey('Status')) { Check ($identity.ReleaseStatus -ceq $case.Status) 'Missing conflict status' }
    if ($identity.ReleaseStatus -eq 'IsoNameHint') { Check ($identity.DetectionNote.Contains('ISO 文件名') -and $identity.DetectionNote.Contains('错误重命名')) 'Filename hint presented as verified internal release' }
    if ($identity.Release) { Check $identity.DisplayName.Contains($identity.Release) 'Display missing release' }
}
$script:failMetadata=$true
$failed=Get-InstallImageIdentity 'H:\sources\install.esd' 'windows_11_25h2.iso'
Check ($failed.VolumeLabel -eq 'WINSETUP' -and -not $failed.Release) 'ISO name bypassed failed image read'
$script:failMetadata=$false

# Only metadata and filesystem probes are allowed in inspection mode.
$script:messages=New-Object 'Collections.Generic.List[string]'
$script:metadata=[pscustomobject]@{ImageName='Windows 11 Education';ImageDescription='';EditionId='Education';MajorVersion=10;MinorVersion=0;Build=26200;SPBuild=1;InstallationType='Client';ProductType='WinNT'}
function Write-Host { param($Object,$ForegroundColor); $script:messages.Add([string]$Object) }
function Assert-Administrator { }
function Get-SourceMedia { param($Letter); return [pscustomobject]@{ImagePath='D:\windows_11_business_editions_version_25h2.iso'} }
function Test-Path { param($LiteralPath,$PathType); return $LiteralPath -eq 'C:\sources\install.wim' }
function Run-DiskPart { throw 'Forbidden formatting' }
function Start-UsbSteps { throw 'Forbidden full USB workflow' }
function Set-Volume { throw 'Forbidden volume changes' }
function Read-Host { throw 'Inspection should not prompt' }
$script:completionHandled=$false
Start-IsoInspection 'C'
Check ($script:completionHandled -and $script:readImage -eq 'C:\sources\install.wim') 'Inspection failed or read wrong source'
Check (($script:messages -join "`n").Contains('WIN11_25H2') -and ($script:messages -join "`n").Contains('只读识别完成')) 'Inspection result missing'

# Assert unrelated USB, progress, copy and formatting helpers did not change.

"PASS: $($cases.Count) current image identity/release/label fixtures; metadata fallback and conflict handling."
