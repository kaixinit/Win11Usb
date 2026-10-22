$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$global:Win11UsbEmbeddedEngine = [IO.File]::ReadAllText((Join-Path $project 'src\engine\Win11Usb.Engine.ps1'))
$global:Win11UsbIconBytes = [IO.File]::ReadAllBytes((Join-Path $project 'assets\usb-eject.ico'))
$global:Win11UsbEmbeddedWorker = [IO.File]::ReadAllText((Join-Path $project 'src\Win11Usb.Worker.ps1'))
$global:Win11UsbPreview = $true
$global:Win11UsbPreviewRunning = $false
$source = [IO.File]::ReadAllText((Join-Path $project 'src\Win11Usb.Gui.ps1'))
$source = $source.Replace('$null = $window.ShowDialog()', '')
. ([scriptblock]::Create($source))
function Assert-Display($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
Assert-Display ($FsCombo.SelectedItem.Tag -eq 'FAT32') 'FAT32 default changed'
$NtfsOption.IsChecked = $true
Assert-Display ($FsCombo.SelectedItem.Tag -eq 'NTFS') 'NTFS did not reach engine selection'
$Fat32Option.IsChecked = $true
Assert-Display ($FsCombo.SelectedItem.Tag -eq 'FAT32') 'FAT32 did not reach engine selection'
Update-ProgressDisplay '[4/6] 复制安装文件和 SWM 到 U 盘 F:\'
Update-ProgressDisplay '复制拆分映像 SWM [======    ] 整体约 68.0% 用时 04:32 | 当前 36.9% install2.swm'
Assert-Display ($script:currentStage -eq 4) 'Phase mapping failed'
Assert-Display ([object]::ReferenceEquals($window.FindName('StageIcon3').Source,$window.Resources['StepDoneImage'])) 'Completed phase was not marked complete'
Assert-Display ([object]::ReferenceEquals($window.FindName('StageIcon4').Source,$window.Resources['StepCurrentImage'])) 'Current phase indicator did not follow engine messages'
Assert-Display ([object]::ReferenceEquals($window.FindName('StageIcon5').Source,$window.Resources['StepPendingImage'])) 'Future phase was shown as complete'
Assert-Display ($MainProgress.Value -eq 68 -and $FileProgress.Value -eq 36.9) 'Overall/file percentages were mixed'
Assert-Display ($ElapsedText.Text -eq '用时 04:32') 'Elapsed time was lost'
Assert-Display ($FileText.Text -match 'install2.swm') 'Current file was lost'
Add-UiLog '复制文件 [==========] 100.0% 已完成'
Assert-Display (-not $script:runSucceeded) 'A finished command incorrectly finished the job'
Add-UiLog '制作完成：F:\，复制文件大小一致，efi\boot\bootx64.efi 已就绪。'
Assert-Display $script:runSucceeded 'Verified result was not recognized'
Add-UiLog '完成：F:\，FAT32，全容量 57.75 GB，一个分区。'
Assert-Display ($DiskCombo.SelectedItem.Metadata -match '已重建 1 个分区') 'Old partition layout remained in disk selector'
$idleHeight = $window.Height
$idleWidth = $window.Width
Show-RunningPreview
Assert-Display ($SetupPanel.Visibility -eq 'Visible' -and $ProgressPanel.Visibility -eq 'Visible' -and $LogBox.Visibility -eq 'Visible') 'Running mode replaced the fixed screen'
Assert-Display (-not $DiskCombo.IsEnabled -and -not $SelectIsoButton.IsEnabled -and -not $StartButton.IsEnabled -and -not $Fat32Option.IsEnabled) 'Running selections were not locked'
Assert-Display ($window.Height -eq $idleHeight -and $window.Width -eq $idleWidth) 'Running mode resized the window'
Assert-Display ($null -eq $script:engine -and $null -eq $script:engineRunspace -and -not $script:busy) 'Preview created an engine'
Set-RunLayout $false
Assert-Display ($SetupPanel.Visibility -eq 'Visible' -and $LogBox.Visibility -eq 'Visible' -and $DiskCombo.IsEnabled) 'Idle mode hid the log or failed to unlock'
Assert-Display ($window.Height -eq $idleHeight -and $window.Width -eq $idleWidth) 'Unlocking resized the window'
Start-Engine
Assert-Display ($null -eq $script:engine) 'Preview reached Start-Engine'
Set-StageDisplay 6 -Complete
Assert-Display ([object]::ReferenceEquals($window.FindName('StageIcon6').Source,$window.Resources['StepDoneImage'])) 'Successful completion did not finish the stage indicator'
$PreviewEmpty=$true
Refresh-Devices
Set-RunLayout $false
Assert-Display ($DiskCombo.Items.Count -eq 0 -and -not $DiskCombo.IsEnabled -and -not $StartButton.IsEnabled) 'Empty device state allowed starting or selecting a nonexistent device'
Assert-Display ($DiskPlaceholder.Visibility -eq 'Visible') 'Empty device state lost its placeholder'
Show-RunningPreview
Assert-Display ($null -eq $script:engine -and -not $script:showingRun) 'Empty preview entered a running state'
$window.Close()
'Display checks passed: filesystem mapping, separate progress, verified result, disk metadata, preview isolation, fixed layout, input locking, persistent logs.'
