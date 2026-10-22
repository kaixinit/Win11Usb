#requires -Version 5.1
param([ValidateSet('All','Start','Format','Usb','Iso','Cleanup','Result','Failure','Stop','Close')][string]$Dialog='Format',[ValidateSet('FAT32','NTFS')][string]$FileSystem='NTFS')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$global:Win11UsbEmbeddedEngine=[IO.File]::ReadAllText((Join-Path $root 'src\engine\Win11Usb.Engine.ps1'))
$global:Win11UsbEmbeddedWorker=[IO.File]::ReadAllText((Join-Path $root 'src\Win11Usb.Worker.ps1'))
$global:Win11UsbIconBytes=[IO.File]::ReadAllBytes((Join-Path $root 'assets\usb-eject.ico'))
$global:Win11UsbPreview=$true
$source=[IO.File]::ReadAllText((Join-Path $root 'src\Win11Usb.Gui.ps1')).Replace('$null = $window.ShowDialog()','')
. ([scriptblock]::Create($source))
$window.Title='Windows 安装 U 盘 - 弹窗预览（不会修改磁盘）'
if($FileSystem -eq 'NTFS'){$NtfsOption.IsChecked=$true}
$window.Show()
try {
    $previewNames=if($Dialog -eq 'All'){@('Start','Format','Usb','Iso','Cleanup','Result','Failure','Stop','Close')}else{@($Dialog)}
    foreach($previewKind in $previewNames){
    switch($previewKind){
        'Start'{$null=Confirm-StartSelection}
        'Stop'{$null=Confirm-StopWork}
        'Close'{$null=Confirm-StopWork -Closing}
        {$_ -in @('Result','Failure')} {
            $script:engineState=[hashtable]::Synchronized(@{WorkRoot=(Join-Path $PSScriptRoot 'artifacts/dialog-preview');ExitCode=$(if($previewKind -eq 'Result'){0}else{1});CopyVerified=($previewKind -eq 'Result');TargetRebuildStarted=($previewKind -eq 'Failure');LastError='复制失败示例：日志已保留，可核对镜像与设备后重试。';InstallName='Windows 11 教育版 26H2';VolumeLabel='WIN11_26H2';UsbResult='U 盘保持连接，可稍后安全弹出。';IsoResult='ISO 保持挂载。'})
            Show-RunResult
        }
        default {
            $prompt=switch($previewKind){'Format'{'请输入编号（回车默认取消）'}'Usb'{'按回车安全弹出 U 盘 F:；输入 Q 跳过'}'Iso'{'按回车弹出已识别的 ISO / DVD（H:）；输入 Q 退出并保留挂载'}'Cleanup'{'按回车清理；输入 Q 保留临时文件'}}
            $r=[pscustomobject]@{Kind='Prompt';Index=$(if($previewKind -eq 'Format'){4}else{8});Prompt=$prompt;Response='';Cancelled=$false;Ready=[Threading.ManualResetEvent]::new($false)}
            try{Invoke-UiPrompt $r}finally{$r.Ready.Dispose()}
        }
    }
    }
}finally{$window.Close()}
