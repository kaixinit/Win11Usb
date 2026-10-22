#requires -Version 5.1
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$global:Win11UsbEmbeddedEngine=[IO.File]::ReadAllText((Join-Path $root 'src\engine\Win11Usb.Engine.ps1'))
$global:Win11UsbEmbeddedWorker=[IO.File]::ReadAllText((Join-Path $root 'src\Win11Usb.Worker.ps1'))
$global:Win11UsbIconBytes=[IO.File]::ReadAllBytes((Join-Path $root 'assets\usb-eject.ico'))
$global:Win11UsbPreview=$true
$source=[IO.File]::ReadAllText((Join-Path $root 'src\Win11Usb.Gui.ps1')).Replace('$null = $window.ShowDialog()','')
. ([scriptblock]::Create($source))
function Check($Value,$Message){if(-not $Value){throw $Message}}
function Show-UiDialog {
    param($Title,$Heading,$Message,$Details,$Hint,$Kind,$PrimaryText,$SecondaryText,$DefaultPrimary=$true)
    $script:lastDialog=[pscustomobject]@{Title=$Title;Heading=$Heading;Message=$Message;Details=$Details;Kind=$Kind;Primary=$PrimaryText;Secondary=$SecondaryText;Default=$DefaultPrimary}
    return $script:decision
}
function Show-FormatConfirmation {param($ConfirmationDetails);$script:formatDetails=$ConfirmationDetails;return $script:decision}
function Request($Prompt,$Index=8){return [pscustomobject]@{Kind='Prompt';Index=$Index;Prompt=$Prompt;Response='';Cancelled=$false;Ready=[Threading.ManualResetEvent]::new($false)}}
try {
    foreach($fs in @('FAT32','NTFS')){
        if($fs -eq 'NTFS'){$NtfsOption.IsChecked=$true}else{$Fat32Option.IsChecked=$true}
        foreach($choice in @($false,$true)){
            $script:decision=$choice;$r=Request '请输入编号（回车默认取消）' 4
            Invoke-UiPrompt $r
            Check ($r.Response -eq $(if($choice){'1'}else{'2'})) 'Format reply mismatch'
            Check $r.Ready.WaitOne(0) 'Format prompt was not released'
            if($fs -eq 'NTFS'){Check $script:formatDetails.Contains('部分 UEFI 固件可能不支持') 'NTFS warning missing from destructive confirmation'}
            $r.Ready.Dispose()
        }
    }
    foreach($prompt in @('按回车安全弹出 U 盘 F:；输入 Q 跳过','按回车弹出已识别的 ISO / DVD（H:）；输入 Q 退出并保留挂载','按回车清理；输入 Q 保留临时文件')){
        foreach($choice in @($false,$true)){
            $script:decision=$choice;$r=Request $prompt
            Invoke-UiPrompt $r
            Check ($r.Response -ceq $(if($choice){''}else{'Q'})) 'Optional step reply mismatch'
            Check $r.Ready.WaitOne(0) 'Optional prompt was not released'
            $r.Ready.Dispose()
        }
    }
    foreach($prompt in @('按回车执行本步；输入 Q 退出','按回车执行本机拆分；输入 Q 退出','按回车跳过拆分并开始复制；输入 Q 退出')){
        $r=Request $prompt;Invoke-UiPrompt $r
        Check (-not $r.Cancelled -and $r.Response -eq '' -and $r.Ready.WaitOne(0)) 'Known step pause failed'
        $r.Ready.Dispose()
    }
    $r=Request 'UNKNOWN potentially destructive confirmation'
    Invoke-UiPrompt $r
    Check ($r.Cancelled -and $r.Ready.WaitOne(0)) 'Unknown prompt was auto-approved'
    $r.Ready.Dispose()
    $script:runFailed=$false;$script:runSucceeded=$false
    Add-UiLog '正在检查 ISO 文件：C:\制作完成：假复制文件大小一致.iso'
    Check (-not $script:runSucceeded) 'A filename impersonated successful verification'
    for($i=1;$i -le 700;$i++){$script:promptQueue.Add([pscustomobject]@{Kind='Log';Text="tail-$i"})}
    Receive-EngineMessages
    Check ($script:promptQueue.Count -eq 200) 'Bounded tick did not protect UI responsiveness'
    Receive-EngineMessages -All
    Check ($LogBox.Text.Contains('tail-700') -and $script:promptQueue.Count -eq 0) 'Completion drain dropped tail logs'
    $directory=Join-Path $PSScriptRoot 'artifacts/prompts'
    foreach($case in @('Success','Cancelled','Error','PostVerifyError','UnknownResult')){
        $script:runFailed=$false
        $script:engineState=[hashtable]::Synchronized(@{WorkRoot=$directory;ExitCode=1;CopyVerified=$false;TargetRebuildStarted=$false;LastError='fixture error';InstallName='Windows 11';VolumeLabel='WIN11';UsbResult='U 盘尚未安全弹出：veto';IsoResult='ISO 保持挂载。'})
        switch($case){
            'Success'{$script:engineState.ExitCode=0;$script:engineState.CopyVerified=$true}
            'Cancelled'{$script:engineState.LastError='已取消整盘格式化'}
            'PostVerifyError'{$script:engineState.CopyVerified=$true;$script:engineState.TargetRebuildStarted=$true}
            'UnknownResult'{$script:engineState.ExitCode=$null}
        }
        Show-RunResult
        Check ($script:runSucceeded -eq ($case -eq 'Success')) "False success: $case"
        if($case -eq 'Success'){Check $script:lastDialog.Details.Contains('尚未安全弹出') 'Eject warning disappeared from success summary'}
        if($case -eq 'Cancelled'){Check $script:lastDialog.Message.Contains('未被修改') 'Cancellation did not describe disk state'}
        if($case -eq 'PostVerifyError'){Check $script:lastDialog.Message.Contains('后续步骤') 'Post-verification failure was misrepresented'}
    }
    Check (Test-Path -LiteralPath (Join-Path $directory 'session.log')) 'Session log was not saved'
    'PASS: popup routing, cancel/confirm/optional replies, NTFS warning, unknown prompt rejection, final-state truth, queued tail drain, persisted session log.'
}finally{$window.Close()}
