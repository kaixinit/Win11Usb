#requires -Version 5.1
[CmdletBinding()]
param([switch]$Preview, [switch]$PreviewRunning, [switch]$PreviewEmpty)

$ErrorActionPreference = 'Stop'
$enginePath = ''
if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    $enginePath = Join-Path $PSScriptRoot 'engine\Win11Usb.Engine.ps1'
}
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
$script:workerScript = [string]$global:Win11UsbEmbeddedWorker
if (-not $script:workerScript -and $PSScriptRoot) {
    $workerPath = Join-Path $PSScriptRoot 'Win11Usb.Worker.ps1'
    if (Test-Path -LiteralPath $workerPath) { $script:workerScript = [IO.File]::ReadAllText($workerPath) }
}
$script:engineScript = [string]$global:Win11UsbEmbeddedEngine
if ([string]::IsNullOrWhiteSpace($script:engineScript) -and (Test-Path -LiteralPath $enginePath -PathType Leaf)) {
    $script:engineScript = Get-Content -LiteralPath $enginePath -Raw
}
if ([string]::IsNullOrWhiteSpace($script:engineScript)) {
    [System.Windows.MessageBox]::Show("找不到制作引擎：`n$enginePath", 'Windows 安装 U 盘', 'OK', 'Error') | Out-Null
    exit 1
}
if ($global:Win11UsbPreview) { $Preview = $true }
if ($global:Win11UsbPreviewRunning -or $PreviewRunning) { $Preview = $true; $PreviewRunning = $true }
if ($global:Win11UsbPreviewEmpty -or $PreviewEmpty) { $Preview = $true; $PreviewEmpty = $true }

# Run elevated because partition creation and formatting require administrator rights.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin -and -not $Preview) {
    throw '直接运行脚本请使用管理员 PowerShell；图形版请运行 Win11Usb.exe，由 Windows 请求所需权限。'
}

[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
 Title="Windows 安装 U 盘制作工具" Width="840" Height="800" MinWidth="720" MinHeight="700"
 WindowStartupLocation="CenterScreen" Background="#FAFCFF" Foreground="#172B48"
 FontFamily="Microsoft YaHei UI" FontSize="14" UseLayoutRounding="True">
 <Window.Resources>
  <Geometry x:Key="IconIso">F1 M6 2C4.89 2 4 2.89 4 4V20A2 2 0 0 0 6 22H12.68A7 7 0 0 1 12 19A7 7 0 0 1 19 12A7 7 0 0 1 20 12.08V8L14 2H6M13 3.5L18.5 9H13V3.5M18 14C17.87 14 17.76 14.09 17.74 14.21L17.55 15.53C17.25 15.66 16.96 15.82 16.7 16L15.46 15.5C15.35 15.5 15.22 15.5 15.15 15.63L14.15 17.36C14.09 17.47 14.11 17.6 14.21 17.68L15.27 18.5C15.25 18.67 15.24 18.83 15.24 19C15.24 19.17 15.25 19.33 15.27 19.5L14.21 20.32C14.12 20.4 14.09 20.53 14.15 20.64L15.15 22.37C15.21 22.5 15.34 22.5 15.46 22.5L16.7 22C16.96 22.18 17.24 22.35 17.55 22.47L17.74 23.79C17.76 23.91 17.86 24 18 24H20C20.11 24 20.22 23.91 20.24 23.79L20.43 22.47C20.73 22.34 21 22.18 21.27 22L22.5 22.5C22.63 22.5 22.76 22.5 22.83 22.37L23.83 20.64C23.89 20.53 23.86 20.4 23.77 20.32L22.7 19.5C22.72 19.33 22.74 19.17 22.74 19C22.74 18.83 22.73 18.67 22.7 18.5L23.76 17.68C23.85 17.6 23.88 17.47 23.82 17.36L22.82 15.63C22.76 15.5 22.63 15.5 22.5 15.5L21.27 16C21 15.82 20.73 15.65 20.42 15.53L20.23 14.21C20.22 14.09 20.11 14 20 14H18M19 17.5C19.83 17.5 20.5 18.17 20.5 19C20.5 19.83 19.83 20.5 19 20.5C18.16 20.5 17.5 19.83 17.5 19C17.5 18.17 18.17 17.5 19 17.5Z</Geometry>
  <Geometry x:Key="IconUsb">F1 M8 15C8.55 15 9 15.45 9 16C9 16.55 8.55 17 8 17C7.45 17 7 16.55 7 16C7 15.45 7.45 15 8 15M15.07 4.69L16.5 6.1L15.07 7.5L13.66 6.1L15.07 4.69M17.9 7.5L19.31 8.93L17.9 10.34L16.5 8.93L17.9 7.5M8 13C6.34 13 5 14.34 5 16C5 17.66 6.34 19 8 19C9.66 19 11 17.66 11 16C11 14.34 9.66 13 8 13M9.77 4.33L10.5 5.08L14.29 1.29C14.47 1.11 14.72 1 15 1C15.28 1 15.53 1.11 15.71 1.29L22.78 8.36L22.78 8.37C22.92 8.54 23 8.76 23 9C23 9.3 22.87 9.57 22.66 9.76L22.66 9.76L18.93 13.5L19.67 14.23L12.95 20.95C11.68 22.22 9.93 23 8 23C4.13 23 1 19.87 1 16C1 14.07 1.78 12.32 3.05 11.05L9.77 4.33M20.59 9L15 3.41L11.93 6.5L17.5 12.08L20.59 9Z</Geometry>
  <Geometry x:Key="IconSettings">F1 M12,15.5A3.5,3.5 0 0,1 8.5,12A3.5,3.5 0 0,1 12,8.5A3.5,3.5 0 0,1 15.5,12A3.5,3.5 0 0,1 12,15.5M19.43,12.97C19.47,12.65 19.5,12.33 19.5,12C19.5,11.67 19.47,11.34 19.43,11L21.54,9.37C21.73,9.22 21.78,8.95 21.66,8.73L19.66,5.27C19.54,5.05 19.27,4.96 19.05,5.05L16.56,6.05C16.04,5.66 15.5,5.32 14.87,5.07L14.5,2.42C14.46,2.18 14.25,2 14,2H10C9.75,2 9.54,2.18 9.5,2.42L9.13,5.07C8.5,5.32 7.96,5.66 7.44,6.05L4.95,5.05C4.73,4.96 4.46,5.05 4.34,5.27L2.34,8.73C2.21,8.95 2.27,9.22 2.46,9.37L4.57,11C4.53,11.34 4.5,11.67 4.5,12C4.5,12.33 4.53,12.65 4.57,12.97L2.46,14.63C2.27,14.78 2.21,15.05 2.34,15.27L4.34,18.73C4.46,18.95 4.73,19.03 4.95,18.95L7.44,17.94C7.96,18.34 8.5,18.68 9.13,18.93L9.5,21.58C9.54,21.82 9.75,22 10,22H14C14.25,22 14.46,21.82 14.5,21.58L14.87,18.93C15.5,18.67 16.04,18.34 16.56,17.94L19.05,18.95C19.27,19.03 19.54,18.95 19.66,18.73L21.66,15.27C21.78,15.05 21.73,14.78 21.54,14.63L19.43,12.97Z</Geometry>
  <Geometry x:Key="IconStage">F1 M7,5H21V7H7V5M7,13V11H21V13H7M4,4.5A1.5,1.5 0 0,1 5.5,6A1.5,1.5 0 0,1 4,7.5A1.5,1.5 0 0,1 2.5,6A1.5,1.5 0 0,1 4,4.5M4,10.5A1.5,1.5 0 0,1 5.5,12A1.5,1.5 0 0,1 4,13.5A1.5,1.5 0 0,1 2.5,12A1.5,1.5 0 0,1 4,10.5M7,19V17H21V19H7M4,16.5A1.5,1.5 0 0,1 5.5,18A1.5,1.5 0 0,1 4,19.5A1.5,1.5 0 0,1 2.5,18A1.5,1.5 0 0,1 4,16.5Z</Geometry>
  <Geometry x:Key="IconLog">F1 M20,19V7H4V19H20M20,3A2,2 0 0,1 22,5V19A2,2 0 0,1 20,21H4A2,2 0 0,1 2,19V5C2,3.89 2.9,3 4,3H20M13,17V15H18V17H13M9.58,13L5.57,9H8.4L11.7,12.3C12.09,12.69 12.09,13.33 11.7,13.72L8.42,17H5.59L9.58,13Z</Geometry>
  <Geometry x:Key="IconRefresh">F1 M17.65,6.35C16.2,4.9 14.21,4 12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20C15.73,20 18.84,17.45 19.73,14H17.65C16.83,16.33 14.61,18 12,18A6,6 0 0,1 6,12A6,6 0 0,1 12,6C13.66,6 15.14,6.69 16.22,7.78L13,11H20V4L17.65,6.35Z</Geometry>
  <Geometry x:Key="IconChevron">F1 M7.41,8.58L12,13.17L16.59,8.58L18,10L12,16L6,10L7.41,8.58Z</Geometry>
  <Geometry x:Key="IconWarning">F1 M12,2L1,21H23M12,6L19.53,19H4.47M11,10V14H13V10M11,16V18H13V16</Geometry>
  <Geometry x:Key="IconInfo">F1 M11,9H13V7H11M12,20C7.59,20 4,16.41 4,12C4,7.59 7.59,4 12,4C16.41,4 20,7.59 20,12C20,16.41 16.41,20 12,20M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M11,17H13V11H11V17Z</Geometry>
  <Geometry x:Key="IconPlay">F1 M8,5.14V19.14L19,12.14L8,5.14Z</Geometry>
  <Geometry x:Key="IconStepDone">F1 M12 2C6.5 2 2 6.5 2 12S6.5 22 12 22 22 17.5 22 12 17.5 2 12 2M10 17L5 12L6.41 10.59L10 14.17L17.59 6.58L19 8L10 17Z</Geometry>
  <Geometry x:Key="IconStepCurrent">F1 M12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,7A5,5 0 0,0 7,12A5,5 0 0,0 12,17A5,5 0 0,0 17,12A5,5 0 0,0 12,7Z</Geometry>
  <Geometry x:Key="IconStepPending">F1 M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z</Geometry>
  <DrawingImage x:Key="IsoImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2165DF" Geometry="{StaticResource IconIso}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="UsbImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2165DF" Geometry="{StaticResource IconUsb}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="SettingsImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2165DF" Geometry="{StaticResource IconSettings}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="StageImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2165DF" Geometry="{StaticResource IconStage}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="LogImage"><DrawingImage.Drawing><GeometryDrawing Brush="#7CB3FF" Geometry="{StaticResource IconLog}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="RefreshImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2165DF" Geometry="{StaticResource IconRefresh}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="ChevronImage"><DrawingImage.Drawing><GeometryDrawing Brush="#76859A" Geometry="{StaticResource IconChevron}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="WarningImage"><DrawingImage.Drawing><GeometryDrawing Brush="#B56C0A" Geometry="{StaticResource IconWarning}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="InfoImage"><DrawingImage.Drawing><GeometryDrawing Brush="#76859A" Geometry="{StaticResource IconInfo}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="PlayImage"><DrawingImage.Drawing><GeometryDrawing Brush="White" Geometry="{StaticResource IconPlay}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="StepDoneImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2878F0" Geometry="{StaticResource IconStepDone}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="StepCurrentImage"><DrawingImage.Drawing><GeometryDrawing Brush="#2878F0" Geometry="{StaticResource IconStepCurrent}"/></DrawingImage.Drawing></DrawingImage>
  <DrawingImage x:Key="StepPendingImage"><DrawingImage.Drawing><GeometryDrawing Brush="#DCE4EF" Geometry="{StaticResource IconStepPending}"/></DrawingImage.Drawing></DrawingImage>
  <Style TargetType="Button">
   <Setter Property="Padding" Value="15,9"/><Setter Property="FontWeight" Value="SemiBold"/>
   <Setter Property="Background" Value="#EDF3FD"/><Setter Property="Foreground" Value="#2456A5"/>
   <Setter Property="BorderBrush" Value="#D7E3F4"/><Setter Property="BorderThickness" Value="1"/>
   <Setter Property="Cursor" Value="Hand"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
    <Border x:Name="Surface" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="6" Padding="{TemplateBinding Padding}">
     <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
    </Border>
    <ControlTemplate.Triggers>
     <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Surface" Property="Opacity" Value="0.85"/></Trigger>
     <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Surface" Property="BorderBrush" Value="#2563EB"/><Setter TargetName="Surface" Property="BorderThickness" Value="2"/></Trigger>
     <Trigger Property="IsEnabled" Value="False"><Setter TargetName="Surface" Property="Opacity" Value="0.45"/></Trigger>
    </ControlTemplate.Triggers>
   </ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="ComboBox">
   <Setter Property="Padding" Value="12,9"/><Setter Property="MinHeight" Value="44"/><Setter Property="VerticalContentAlignment" Value="Center"/>
   <Setter Property="Background" Value="#F7FAFF"/><Setter Property="BorderBrush" Value="#CBD7E7"/>
   <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ComboBox"><Grid>
    <ToggleButton Focusable="False" IsChecked="{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}" ClickMode="Press"><ToggleButton.Template><ControlTemplate TargetType="ToggleButton"><Border x:Name="SelectorSurface" Background="{Binding Background, RelativeSource={RelativeSource AncestorType=ComboBox}}" BorderBrush="{Binding BorderBrush, RelativeSource={RelativeSource AncestorType=ComboBox}}" BorderThickness="1" CornerRadius="6"/></ControlTemplate></ToggleButton.Template></ToggleButton>
    <ContentPresenter IsHitTestVisible="False" Content="{TemplateBinding SelectionBoxItem}" ContentTemplate="{TemplateBinding SelectionBoxItemTemplate}" ContentTemplateSelector="{TemplateBinding ItemTemplateSelector}" Margin="12,9,34,9" VerticalAlignment="Center"/>
    <Image Source="{StaticResource ChevronImage}" Width="16" Height="16" HorizontalAlignment="Right" VerticalAlignment="Center" Margin="0,0,12,0" IsHitTestVisible="False"/>
    <Popup x:Name="PART_Popup" Placement="Bottom" IsOpen="{TemplateBinding IsDropDownOpen}" AllowsTransparency="True" Focusable="False"><Border Background="White" BorderBrush="#CBD8EB" BorderThickness="1" CornerRadius="6" MinWidth="{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}" MaxHeight="280" Padding="3"><ScrollViewer CanContentScroll="True"><ItemsPresenter KeyboardNavigation.DirectionalNavigation="Contained"/></ScrollViewer></Border></Popup>
   </Grid><ControlTemplate.Triggers><Trigger Property="IsKeyboardFocusWithin" Value="True"><Setter Property="BorderBrush" Value="#4D82DF"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Background" Value="#F5F7FA"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
  </Style>
  <Style TargetType="ComboBoxItem"><Setter Property="Padding" Value="10,8"/><Setter Property="VerticalContentAlignment" Value="Center"/></Style>
 </Window.Resources>
 <Grid Margin="22,18,22,16">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel x:Name="SetupPanel" Grid.Row="0">
   <Border BorderBrush="#DFE6EF" BorderThickness="0,0,0,1" Padding="0,0,0,18">
    <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="50"/><ColumnDefinition Width="16"/><ColumnDefinition Width="88"/><ColumnDefinition Width="*"/><ColumnDefinition Width="12"/><ColumnDefinition Width="106"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
     <Border Grid.RowSpan="2" Width="50" Height="50" CornerRadius="11" Background="#E8F0FE" VerticalAlignment="Top" Margin="0,3,0,0"><Image Source="{StaticResource IsoImage}" Width="29" Height="31"/></Border>
     <TextBlock Grid.Column="2" Grid.RowSpan="2" Text="安装镜像" FontSize="15" FontWeight="SemiBold" VerticalAlignment="Center"/>
     <Border Grid.Column="3" Background="#F8FAFD" BorderBrush="#CBD7E7" BorderThickness="1" CornerRadius="6" Padding="12,9" MinHeight="44"><TextBlock x:Name="IsoNameText" Text="选择 Windows ISO 镜像" TextWrapping="Wrap" Foreground="#76859A" VerticalAlignment="Center"/></Border>
     <Button x:Name="SelectIsoButton" Grid.Column="5" Content="选择 ISO…" Padding="11,9" VerticalAlignment="Top" MinHeight="44"/>
     <TextBlock x:Name="IsoDirectoryText" Grid.Row="1" Grid.Column="3" Grid.ColumnSpan="3" Text="支持 .iso 文件" FontSize="12.5" Foreground="#64748B" TextWrapping="Wrap" Margin="0,5,0,0"/>
    </Grid>
   </Border>
   <TextBox x:Name="IsoPathBox" Visibility="Collapsed" IsReadOnly="True"/>
   <TextBlock x:Name="IsoInfo" Visibility="Collapsed" Text="开始制作时自动挂载并识别系统版本。" FontSize="12.5" Foreground="#64748B" TextWrapping="Wrap"/>
   <Border BorderBrush="#DFE6EF" BorderThickness="0,0,0,1" Padding="0,18,0,18">
    <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="50"/><ColumnDefinition Width="16"/><ColumnDefinition Width="88"/><ColumnDefinition Width="*"/><ColumnDefinition Width="12"/><ColumnDefinition Width="106"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
     <Border Width="50" Height="50" CornerRadius="11" Background="#E8F0FE" VerticalAlignment="Top"><Image Source="{StaticResource UsbImage}" Width="24" Height="24" RenderTransformOrigin="0.5,0.5"><Image.RenderTransform><RotateTransform Angle="-45"/></Image.RenderTransform></Image></Border>
     <TextBlock Grid.Column="2" Text="写入U盘" FontSize="15" FontWeight="SemiBold" VerticalAlignment="Center"/>
     <Grid Grid.Column="3">
      <ComboBox x:Name="DiskCombo" HorizontalContentAlignment="Stretch" AutomationProperties.Name="写入U盘，整支物理磁盘">
       <ComboBox.ItemTemplate><DataTemplate><StackPanel><TextBlock Text="{Binding FriendlyName}" FontWeight="SemiBold" FontSize="14" TextWrapping="Wrap"/><TextBlock Text="{Binding Metadata}" FontSize="12" Foreground="#526782" Margin="0,3,0,0" TextWrapping="Wrap"/></StackPanel></DataTemplate></ComboBox.ItemTemplate>
      </ComboBox>
      <TextBlock x:Name="DiskPlaceholder" Text="连接 U 盘后选择设备" Foreground="#76859A" Margin="12,0,30,0" VerticalAlignment="Center" IsHitTestVisible="False"/>
     </Grid>
     <Button x:Name="RefreshButton" Grid.Column="5" Content="刷新" Padding="8,8" BorderThickness="0" Background="Transparent" VerticalAlignment="Top" MinHeight="44" AutomationProperties.Name="刷新设备"><Button.ContentTemplate><DataTemplate><StackPanel Orientation="Horizontal"><Image Source="{StaticResource RefreshImage}" Width="21" Height="21" Margin="0,0,7,0"/><TextBlock Text="{Binding}" VerticalAlignment="Center"/></StackPanel></DataTemplate></Button.ContentTemplate></Button>
     <StackPanel Grid.Row="1" Grid.Column="3" Grid.ColumnSpan="3" Orientation="Horizontal" Margin="0,8,0,0"><Image Source="{StaticResource WarningImage}" Width="15" Height="15" Margin="0,1,6,0" VerticalAlignment="Top"/><TextBlock x:Name="WarningText" Text="制作将清除所选 U 盘的全部分区和文件。" Foreground="#9C570D" FontSize="12.5" TextWrapping="Wrap"/></StackPanel>
     <TextBlock x:Name="DiskInfo" Grid.Row="2" Grid.Column="3" Grid.ColumnSpan="3" Visibility="Collapsed" Text="未检测到 U 盘，请连接后刷新。" Foreground="#64748B" FontSize="12.5" Margin="0,6,0,0" TextWrapping="Wrap"/>
    </Grid>
   </Border>
   <Grid Margin="0,17,0,0"><Grid.ColumnDefinitions><ColumnDefinition Width="50"/><ColumnDefinition Width="16"/><ColumnDefinition Width="88"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
    <Border Width="44" Height="44" CornerRadius="10" Background="#EDF4FF" HorizontalAlignment="Left" VerticalAlignment="Top"><Image Source="{StaticResource SettingsImage}" Width="27" Height="27"/></Border>
    <TextBlock Grid.Column="2" Text="文件系统" FontSize="15" FontWeight="SemiBold" VerticalAlignment="Top" Margin="0,9,0,0"/>
    <StackPanel Grid.Column="3" Margin="0,5,0,0"><WrapPanel><RadioButton x:Name="Fat32Option" Content="FAT32（UEFI 兼容性更广）" GroupName="FileSystems" IsChecked="True" Background="Transparent" VerticalContentAlignment="Center" Margin="0,0,19,5" AutomationProperties.Name="FAT32，UEFI 兼容性更广"/><RadioButton x:Name="NtfsOption" Content="NTFS（部分 UEFI 固件可能不支持启动）" GroupName="FileSystems" Background="Transparent" VerticalContentAlignment="Center" Margin="0,0,0,5" ToolTip="部分 UEFI 固件不支持直接从 NTFS 启动；仍按 4 GiB 规则拆分 install.wim。" AutomationProperties.Name="NTFS，部分 UEFI 固件可能不支持启动"/></WrapPanel><TextBlock Text="install.wim 超过 4 GiB 时按相同规则拆分" Foreground="#64748B" FontSize="12.5" Margin="0,3,0,0" TextWrapping="Wrap"/></StackPanel>
   </Grid>
   <ComboBox x:Name="FsCombo" Visibility="Collapsed" SelectedIndex="0"><ComboBoxItem Tag="FAT32" Content="FAT32"/><ComboBoxItem Tag="NTFS" Content="NTFS"/></ComboBox>
  </StackPanel>
  <Grid x:Name="ProgressPanel" Grid.Row="1" Margin="0,17,0,14">
   <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
   <Border Grid.Row="0" BorderBrush="#DFE6EF" BorderThickness="0,1,0,0" Padding="0,14,0,0"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="32"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <Image Source="{StaticResource StageImage}" Width="22" Height="22" HorizontalAlignment="Left"/>
    <Grid Grid.Column="1" VerticalAlignment="Center"><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><TextBlock x:Name="StepText" Text="" Visibility="Collapsed"/><TextBlock x:Name="PhaseText" Grid.Column="1" Text="准备就绪" FontSize="15" FontWeight="SemiBold" TextTrimming="CharacterEllipsis" ToolTip="{Binding Text, RelativeSource={RelativeSource Self}}"/></Grid>
    <StackPanel Grid.Column="2" Orientation="Horizontal" VerticalAlignment="Center" Margin="12,0,0,0"><TextBlock x:Name="ProgressLabel" Text="当前操作" Foreground="#526782" FontSize="12.5" VerticalAlignment="Center"/><TextBlock x:Name="PercentText" Text="0.0%" FontSize="16" FontWeight="SemiBold" Foreground="#2165DF" Margin="7,0,0,0"/></StackPanel>
   </Grid></Border>
   <Grid x:Name="StageStrip" Grid.Row="1" Height="20" Margin="0,8,0,0" IsHitTestVisible="False" AutomationProperties.Name="制作阶段"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <Grid Grid.Column="0"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon1" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel1" Grid.Column="1" Text="检查镜像" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/><Border x:Name="StageLink1" Grid.Column="2" Height="1" Background="#DDE5EF" Margin="8,0,8,0" VerticalAlignment="Center"/></Grid>
    <Grid Grid.Column="1"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon2" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel2" Grid.Column="1" Text="格式化" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/><Border x:Name="StageLink2" Grid.Column="2" Height="1" Background="#DDE5EF" Margin="8,0,8,0" VerticalAlignment="Center"/></Grid>
    <Grid Grid.Column="2"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon3" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel3" Grid.Column="1" Text="处理映像" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/><Border x:Name="StageLink3" Grid.Column="2" Height="1" Background="#DDE5EF" Margin="8,0,8,0" VerticalAlignment="Center"/></Grid>
    <Grid Grid.Column="3"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon4" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel4" Grid.Column="1" Text="复制文件" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/><Border x:Name="StageLink4" Grid.Column="2" Height="1" Background="#DDE5EF" Margin="8,0,8,0" VerticalAlignment="Center"/></Grid>
    <Grid Grid.Column="4"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon5" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel5" Grid.Column="1" Text="校验" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/><Border x:Name="StageLink5" Grid.Column="2" Height="1" Background="#DDE5EF" Margin="8,0,8,0" VerticalAlignment="Center"/></Grid>
    <Grid Grid.Column="5"><Grid.ColumnDefinitions><ColumnDefinition Width="11"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image x:Name="StageIcon6" Source="{StaticResource StepPendingImage}" Width="10" Height="10"/><TextBlock x:Name="StageLabel6" Grid.Column="1" Text="清理" Margin="5,0,0,0" FontSize="11" Foreground="#64748B" VerticalAlignment="Center"/></Grid>
   </Grid>
   <ProgressBar x:Name="MainProgress" Grid.Row="2" Height="4" Margin="0,5,0,0" Minimum="0" Maximum="100" Foreground="#2878F0" Background="#E2E9F4" BorderThickness="0"/>
   <Grid Grid.Row="3" Margin="0,8,0,0"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock x:Name="FileText" Text="等待开始制作" Foreground="#526782" FontSize="12.5" TextTrimming="CharacterEllipsis" ToolTip="{Binding Text, RelativeSource={RelativeSource Self}}"/><TextBlock x:Name="ElapsedText" Grid.Column="1" Text="" Foreground="#64748B" FontSize="12.5" Margin="10,0,0,0"/></Grid>
   <ProgressBar x:Name="FileProgress" Grid.Row="4" Height="2" Margin="0,5,0,0" Minimum="0" Maximum="100" Foreground="#7BA7FF" Background="#E5EBF5" BorderThickness="0"/>
  </Grid>
  <Border x:Name="LogPanel" Grid.Row="2" Background="#152235" CornerRadius="8" Margin="0,0,0,14" MinHeight="160">
   <Grid><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
    <Border BorderBrush="#304055" BorderThickness="0,0,0,1" Margin="14,0,14,0" Padding="0,12,0,10"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="29"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><Image Source="{StaticResource LogImage}" Width="21" Height="21" HorizontalAlignment="Left"/><TextBlock x:Name="LogHeading" Grid.Column="1" Text="制作日志" FontSize="13.5" FontWeight="SemiBold" Foreground="#E7EFFA" VerticalAlignment="Center"/><TextBlock x:Name="LogStatus" Grid.Column="2" Text="等待开始" Foreground="#AFC0D8" FontSize="12" VerticalAlignment="Center"/></Grid></Border>
    <TextBox x:Name="LogBox" Grid.Row="1" Padding="14,10,14,12" IsReadOnly="True" AcceptsReturn="True" TextWrapping="Wrap" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled" Background="Transparent" Foreground="#DCE6F4" BorderThickness="0" FontFamily="Consolas, Microsoft YaHei UI" FontSize="13" AutomationProperties.Name="制作日志"/>
   </Grid>
  </Border>
  <Grid Grid.Row="3"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><Grid Margin="0,0,12,0"><Grid.ColumnDefinitions><ColumnDefinition Width="22"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Image Source="{StaticResource InfoImage}" Width="15" Height="15" HorizontalAlignment="Left" VerticalAlignment="Center"/><TextBlock x:Name="FooterText" Grid.Column="1" Text="清盘前会再次核对磁盘并单独确认。" Foreground="#64748B" FontSize="12.5" VerticalAlignment="Center" TextWrapping="Wrap"/></Grid><StackPanel Grid.Column="1" Orientation="Horizontal"><Button x:Name="StartButton" Content="开始制作" MinWidth="112" Background="#2563EB" Foreground="White" BorderBrush="#2563EB" IsEnabled="False" Padding="15,9"><Button.ContentTemplate><DataTemplate><StackPanel Orientation="Horizontal"><Image Source="{StaticResource PlayImage}" Width="15" Height="15" Margin="0,0,7,0"/><TextBlock Text="{Binding}"/></StackPanel></DataTemplate></Button.ContentTemplate></Button><Button x:Name="ForceStopButton" Content="强制结束" MinWidth="96" Background="White" Foreground="#BF3430" BorderBrush="#E78D89" Visibility="Collapsed" Margin="8,0,0,0" Padding="15,9"/></StackPanel></Grid>
 </Grid>
</Window>
'@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)
$iconBytes = $global:Win11UsbIconBytes
if ($iconBytes -and $iconBytes.Length -gt 0) {
    $iconStream = New-Object IO.MemoryStream(,$iconBytes)
    try {
        $iconFrame = [Windows.Media.Imaging.BitmapFrame]::Create($iconStream,[Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,[Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
        $iconFrame.Freeze()
        $window.Icon = $iconFrame
    } finally { $iconStream.Dispose() }
} else {
    $iconPath = Join-Path $PSScriptRoot 'Win11Usb.ico'
    if (-not (Test-Path -LiteralPath $iconPath -PathType Leaf)) { $iconPath = Join-Path $PSScriptRoot '..\assets\usb-eject.ico' }
    if (Test-Path -LiteralPath $iconPath -PathType Leaf) { $window.Icon = [Windows.Media.Imaging.BitmapFrame]::Create([Uri]$iconPath) }
}
foreach ($name in 'IsoPathBox','SelectIsoButton','DiskCombo','FsCombo','IsoInfo','DiskInfo','PhaseText','PercentText','MainProgress','LogBox','LogStatus','FooterText','RefreshButton','StartButton','ForceStopButton','SetupPanel','ProgressPanel','IsoNameText','IsoDirectoryText','Fat32Option','NtfsOption','WarningText','StepText','ProgressLabel','FileText','ElapsedText','FileProgress','LogPanel','LogHeading','DiskPlaceholder') {
    Set-Variable -Name $name -Value $window.FindName($name) -Scope Script
}

$script:busy = $false
$script:engine = $null
$script:engineHandle = $null
$script:engineRunspace = $null
$script:promptQueue = New-Object 'System.Collections.Concurrent.BlockingCollection[object]'
$script:engineLog = New-Object System.Text.StringBuilder
$script:engineState = $null
$script:runSucceeded = $false
$script:runFailed = $false
$script:currentStage = 0
$script:showingRun = $false
$script:processingTick = $false
$script:sessionLogDirectory = ''
function Set-StageDisplay([int]$Stage, [switch]$Complete) {
    $script:currentStage = $Stage
    $StepText.Text = if ($Complete) { '6/6 ·' } elseif ($Stage -gt 0) { "$Stage/6 ·" } else { '' }
    for ($number = 1; $number -le 6; $number++) {
        $done = $Complete -or ($Stage -gt 0 -and $number -lt $Stage)
        $current = -not $Complete -and $number -eq $Stage
        $imageKey = if ($done) { 'StepDoneImage' } elseif ($current) { 'StepCurrentImage' } else { 'StepPendingImage' }
        $window.FindName("StageIcon$number").Source = $window.Resources[$imageKey]
        $label = $window.FindName("StageLabel$number")
        $label.Foreground = if ($current) { '#2165DF' } elseif ($done) { '#526782' } else { '#64748B' }
        $label.FontWeight = if ($current) { 'SemiBold' } else { 'Normal' }
        if ($number -lt 6) { $window.FindName("StageLink$number").Background = if ($done) { '#9BBAED' } else { '#DDE5EF' } }
    }
}

function Sync-SelectionDisplay {
    $iso = [string]$IsoPathBox.Text
    $IsoNameText.Text = if ($iso) { [IO.Path]::GetFileName($iso).Replace('_', ('_' + [char]0x200B)) } else { '选择 Windows ISO 镜像' }
    $IsoDirectoryText.Text = if ($iso) { [IO.Path]::GetDirectoryName($iso) } else { '支持 .iso 文件' }
    $IsoNameText.Foreground = if ($iso) { '#24334A' } else { '#76859A' }
    $IsoNameText.ToolTip = $iso
    $disk = $DiskCombo.SelectedItem
    $DiskPlaceholder.Visibility = if ($disk) { 'Collapsed' } else { 'Visible' }
    $DiskCombo.IsEnabled = -not $script:showingRun -and $DiskCombo.Items.Count -gt 0
    $WarningText.Text = if ($disk) { "制作将清除磁盘 $($disk.Number) 的全部分区和文件。" } else { '制作将清除所选 U 盘的全部分区和文件。' }
}

function Set-DiskMetadata([string]$Metadata) {
    $index = $DiskCombo.SelectedIndex
    if ($index -lt 0) { return }
    # PSCustomObject does not notify WPF when a property changes. Replace the
    # item so both the selected field and popup receive the verified summary.
    $replacement = $DiskCombo.SelectedItem.PSObject.Copy()
    $replacement.Metadata = $Metadata
    $DiskCombo.Items[$index] = $replacement
    $DiskCombo.SelectedIndex = $index
}

function Set-RunLayout([bool]$Running) {
    $script:showingRun = $Running
    $SelectIsoButton.IsEnabled = -not $Running
    $DiskCombo.IsEnabled = -not $Running -and $DiskCombo.Items.Count -gt 0
    $Fat32Option.IsEnabled = -not $Running
    $NtfsOption.IsEnabled = -not $Running
    $RefreshButton.IsEnabled = -not $Running
    $StartButton.Content = if ($Running) { '制作中…' } elseif ($Preview -and -not $PreviewEmpty) { '预览制作状态' } else { '开始制作' }
    $StartButton.IsEnabled = -not $Running -and [bool]($DiskCombo.SelectedItem -and $IsoPathBox.Text)
    $ForceStopButton.Visibility = if ($Running) { 'Visible' } else { 'Collapsed' }
}

function Show-RunningPreview {
    if (-not $Preview -or $PreviewEmpty) { return }
    Set-RunLayout $true
    Set-StageDisplay 4
    $PhaseText.Text = '正在复制拆分映像'
    $ProgressLabel.Text = '复制整体'; $PercentText.Text = '68.0%'
    $MainProgress.IsIndeterminate = $false; $MainProgress.Value = 68
    $FileText.Text = '当前文件：install2.swm · 36.9%'; $FileProgress.Value = 36.9
    $ElapsedText.Text = '用时 04:32'
    $LogBox.Clear()
    $sampleFileSystem = [string]$FsCombo.SelectedItem.Tag
    Set-DiskMetadata '磁盘 3 · 57.8 GB · F:\ · 已重建 1 个分区（示例）'
    foreach ($line in '[预览] 以下为模拟制作状态，不执行磁盘操作。','[检查] Windows ISO 已就绪','[检查] 已识别 Windows 11 教育版 26H2','[检查] 目标磁盘身份核验通过','[完成] 已清除旧分区表','[完成] 已创建全容量主分区',("[完成] {0} 格式化，单分区 57.75 GB" -f $sampleFileSystem),'[检查] install.wim 大小 7.48 GiB，需要拆分','[完成] 已生成 2 个 SWM 文件','[复制] 安装文件已完成','[复制] install.swm · 31.7%','[复制] install.swm · 59.0%','[复制] install.swm · 82.0%','[复制] install.swm · 100.0%','[复制] install2.swm · 10.2%','[复制] install2.swm · 23.5%','[复制] install2.swm · 36.9%') { $LogBox.AppendText($line + [Environment]::NewLine) }
    $LogBox.ScrollToEnd()
    $LogStatus.Text = '模拟进度'
    $FooterText.Text = '预览模式 · 请保持 U 盘连接，完成后将校验复制结果。'
    $ForceStopButton.Visibility = 'Visible'; $ForceStopButton.IsEnabled = $true
    $window.UpdateLayout()
    $LogBox.ScrollToEnd()
}

function Show-UiDialog {
    param(
        [string]$Title,
        [string]$Heading,
        [string]$Message,
        [string]$Details = '',
        [string]$Hint = '',
        [ValidateSet('Info','Warning','Error')][string]$Kind = 'Info',
        [string]$PrimaryText = '确定',
        [string]$SecondaryText = '',
        [bool]$DefaultPrimary = $true
    )
    $accent = switch ($Kind) { 'Warning' { '#D97706' } 'Error' { '#C83B35' } default { '#2878E8' } }
    $glyph = switch ($Kind) { 'Warning' { '!' } 'Error' { '×' } default { 'i' } }
    $kindLabel = switch ($Kind) { 'Warning' { '需要确认' } 'Error' { '出现问题' } default { '提示信息' } }
    [xml]$dialogXaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Windows 安装 U 盘" Width="520" SizeToContent="Height" MaxHeight="680"
        MinWidth="440" WindowStartupLocation="CenterOwner" ResizeMode="NoResize"
        ShowInTaskbar="False" Background="#F3F6FB" FontFamily="Microsoft YaHei UI" FontSize="13">
  <Window.Resources>
    <Style TargetType="Button">
      <Setter Property="Padding" Value="17,9"/><Setter Property="FontWeight" Value="SemiBold"/>
      <Setter Property="BorderThickness" Value="0"/><Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border Background="{TemplateBinding Background}" CornerRadius="7" Padding="{TemplateBinding Padding}"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border></ControlTemplate></Setter.Value></Setter>
    </Style>
  </Window.Resources>
  <Grid Margin="20">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <DockPanel Grid.Row="0" Margin="0,0,0,13">
      <Border x:Name="IconBadge" Width="38" Height="38" CornerRadius="19" Background="#E6F0FF" DockPanel.Dock="Left" Margin="0,0,12,0">
        <TextBlock x:Name="IconGlyph" Text="i" FontSize="23" FontWeight="Bold" HorizontalAlignment="Center" VerticalAlignment="Center"/>
      </Border>
      <StackPanel VerticalAlignment="Center">
        <TextBlock x:Name="KindLabel" Text="提示信息" FontSize="11" FontWeight="SemiBold" Foreground="#718098"/>
        <TextBlock x:Name="HeadingText" Text="提示标题" FontSize="18" FontWeight="Bold" Foreground="#18263D" Margin="0,2,0,0" TextWrapping="Wrap"/>
      </StackPanel>
    </DockPanel>
    <TextBlock x:Name="MessageText" Grid.Row="1" TextWrapping="Wrap" LineHeight="22" Foreground="#34445D" Margin="0,0,0,12"/>
    <Border x:Name="DetailsCard" Grid.Row="2" Background="White" BorderBrush="#DFE6F0" BorderThickness="1" CornerRadius="8" Padding="12" Margin="0,0,0,13" Visibility="Collapsed">
      <ScrollViewer MaxHeight="190" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
        <TextBlock x:Name="DetailsText" TextWrapping="Wrap" LineHeight="21" Foreground="#27364D"/>
      </ScrollViewer>
    </Border>
    <DockPanel Grid.Row="3" LastChildFill="False">
      <TextBlock x:Name="HintText" Foreground="#718098" VerticalAlignment="Center" TextWrapping="Wrap" MaxWidth="250" DockPanel.Dock="Left"/>
      <StackPanel Orientation="Horizontal" DockPanel.Dock="Right">
        <Button x:Name="SecondaryButton" Content="取消" Background="#E7EDF6" Foreground="#26354D" Margin="0,0,8,0"/>
        <Button x:Name="PrimaryButton" Content="确定" Background="#2878E8" Foreground="White"/>
      </StackPanel>
    </DockPanel>
  </Grid>
</Window>
'@
    $dialog = [Windows.Markup.XamlReader]::Load((New-Object System.Xml.XmlNodeReader $dialogXaml))
    $dialog.Owner = $window
    $dialog.Title = $Title
    $dialog.Icon = $window.Icon
    $dialog.FindName('KindLabel').Text = $kindLabel
    $dialog.FindName('HeadingText').Text = $Heading
    $dialog.FindName('MessageText').Text = $Message
    $dialog.FindName('HintText').Text = $Hint
    $badge = $dialog.FindName('IconBadge')
    $badge.Background = [Windows.Media.BrushConverter]::new().ConvertFromString($(if ($Kind -eq 'Warning') { '#FFF2D9' } elseif ($Kind -eq 'Error') { '#FDE8E7' } else { '#E6F0FF' }))
    $dialog.FindName('IconGlyph').Text = $glyph
    $dialog.FindName('IconGlyph').Foreground = [Windows.Media.BrushConverter]::new().ConvertFromString($accent)
    $primary = $dialog.FindName('PrimaryButton')
    $secondary = $dialog.FindName('SecondaryButton')
    $primary.Content = $PrimaryText
    $primary.Background = [Windows.Media.BrushConverter]::new().ConvertFromString($accent)
    if ([string]::IsNullOrWhiteSpace($Details)) {
        $dialog.FindName('DetailsCard').Visibility = 'Collapsed'
    } else {
        $dialog.FindName('DetailsText').Text = $Details
        $dialog.FindName('DetailsCard').Visibility = 'Visible'
    }
    if ([string]::IsNullOrWhiteSpace($SecondaryText)) {
        $secondary.Visibility = 'Collapsed'
        $primary.IsCancel = $true
    } else {
        $secondary.Content = $SecondaryText
        $secondary.IsCancel = $true
        $secondary.IsDefault = -not $DefaultPrimary
    }
    $primary.IsDefault = $DefaultPrimary
    $primary.Add_Click({ $dialog.DialogResult = $true })
    $secondary.Add_Click({ $dialog.DialogResult = $false })
    $dialog.Add_ContentRendered({ if ($DefaultPrimary) { $primary.Focus() | Out-Null } else { $secondary.Focus() | Out-Null } })
    return [bool]$dialog.ShowDialog()
}

function Add-UiLog([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return }
    [void]$script:engineLog.AppendLine($Text)
    $LogBox.AppendText($Text + [Environment]::NewLine)
    $LogBox.ScrollToEnd()
    if ($Text -match '^制作完成：.+复制文件大小一致') { $script:runSucceeded = $true }
    if ($Text.Trim() -match '^(已停止|准备失败)：') { $script:runFailed = $true }
    if ($Text -match '^安装系统：(.+)$') { $IsoInfo.Text = '识别结果：' + $Matches[1]; $IsoNameText.ToolTip = $IsoPathBox.Text + [Environment]::NewLine + $IsoInfo.Text }
    if ($Text -match '^本次临时目录：(.+)$') { $script:sessionLogDirectory = $Matches[1] }
    if ($Text -match '^完成：([A-Z]):\\，(FAT32|NTFS)，全容量') {
        Set-DiskMetadata ('磁盘 {0} · {1:N1} GB · {2}:\ · 已重建 1 个分区' -f $DiskCombo.SelectedItem.Number,($DiskCombo.SelectedItem.Size/1GB),$Matches[1])
    }
}

function Update-ProgressDisplay([string]$Text) {
    $Text = $Text.Trim()
    if ($Text -match '^\[(\d+)/6\]\s*(.*)$') {
        Set-StageDisplay ([int]$Matches[1])
        $PhaseText.Text = $Matches[2]
        $MainProgress.Value = 0; $MainProgress.IsIndeterminate = $true
        $PercentText.Text = ''; $ProgressLabel.Text = '当前操作'
        $FileText.Text = '等待当前文件进度…'; $FileProgress.Value = 0; $ElapsedText.Text = ''
    } elseif ($Text -match '^>\s*(.+)$') {
        $PhaseText.Text = $Matches[1]
        $MainProgress.Value = 0; $MainProgress.IsIndeterminate = $true; $PercentText.Text = ''
        $ProgressLabel.Text = if ($Text -match '复制') { '复制整体' } else { '当前操作' }
        $FileProgress.Value = 0; $FileText.Text = '等待当前文件进度…'; $ElapsedText.Text = ''
    }
    # Overall percentage always precedes the current-file percentage in engine output.
    if ($Text -match '(?:整体约\s*)?(\d+(?:\.\d+)?)%') {
        $value = [Math]::Min(100,[double]$Matches[1])
        $PercentText.Text = ('{0:0.0}%' -f $value); $MainProgress.Value = $value; $MainProgress.IsIndeterminate = $false
    }
    if ($Text -match '当前\s+(\d+(?:\.\d+)?)%\s+(.+)$') {
        $FileProgress.Value = [Math]::Min(100,[double]$Matches[1])
        $FileText.Text = '当前文件：' + $Matches[2] + ' · ' + $Matches[1] + '%'
    }
    if ($Text -match '用时\s+(\d+:\d+)') { $ElapsedText.Text = '用时 ' + $Matches[1] }
    if ($Text -match '^复制') { $ProgressLabel.Text = '复制整体' }
}

function Refresh-Devices {
    $DiskCombo.Items.Clear()
    if ($Preview) {
        if ($PreviewEmpty) {
            $IsoPathBox.Text = ''
            $IsoInfo.Text = '空状态预览；不会读取或修改磁盘。'
            $DiskInfo.Text = '未检测到 U 盘，请连接后刷新。'
            $DiskInfo.Visibility = 'Visible'
            $StartButton.Content = '开始制作'
            $StartButton.IsEnabled = $false
            Sync-SelectionDisplay
            return
        }
        $IsoPathBox.Text = 'D:\Windows ISO\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06.iso'
        [void]$DiskCombo.Items.Add([pscustomobject]@{ Number=3; FriendlyName='Kingston DataTraveler 3.0'; Size=[long](57.8GB); PartitionCount=1; Metadata='磁盘 3 · 57.8 GB · F:\ · 当前 1 个分区（示例）'; Display='磁盘 3 | Kingston DataTraveler 3.0 | 57.8 GB | 当前 1 个分区（示例）' })
        $DiskCombo.SelectedIndex = 0
        $IsoInfo.Text = '识别结果：Windows 11 教育版 26H2 · WIN11_26H2（示例）'
        $DiskInfo.Text = '示例设备。预览模式不会启动制作或修改磁盘。'
        $DiskInfo.Visibility = 'Collapsed'
        $StartButton.Content = '预览制作状态'
        $StartButton.IsEnabled = $true
        Sync-SelectionDisplay
        return
    }
    $disks = @(Get-Disk -ErrorAction SilentlyContinue | Where-Object {
        [string]$_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem -and
        -not $_.IsOffline -and -not $_.IsReadOnly -and $_.Size -ge 8GB
    } | Sort-Object Number)
    foreach ($disk in $disks) {
        $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$($disk.Number)" -ErrorAction SilentlyContinue)
        if ($devices.Count -ne 1 -or -not $devices[0].PNPDeviceID) { continue }
        $partitions = @(Get-Partition -DiskNumber $disk.Number -ErrorAction SilentlyContinue)
        $letters = @($partitions | Where-Object { $_.DriveLetter -and [int][char]$_.DriveLetter -ge 65 } | ForEach-Object { "$($_.DriveLetter):\" })
        $metadata = ('磁盘 {0} · {1:N1} GB · {2} · 当前 {3} 个分区' -f $disk.Number,($disk.Size/1GB),$(if($letters.Count){$letters -join ' / '}else{'未分配盘符'}),$partitions.Count)
        $item = [pscustomobject]@{ Number=[int]$disk.Number; FriendlyName=[string]$disk.FriendlyName; Size=[long]$disk.Size; Path=[string]$disk.Path; SerialNumber=[string]$disk.SerialNumber; PnpId=[string]$devices[0].PNPDeviceID; PartitionCount=$partitions.Count; Metadata=$metadata; Display=('整盘：磁盘 {0}  |  {1}  |  总容量 {2:N1} GB  |  当前 {3} 个分区' -f $disk.Number,$disk.FriendlyName,($disk.Size/1GB),$partitions.Count) }
        [void]$DiskCombo.Items.Add($item)
    }
    if ($DiskCombo.Items.Count -gt 0) { $DiskCombo.SelectedIndex = 0 }
    $StartButton.IsEnabled = (-not [string]::IsNullOrWhiteSpace($IsoPathBox.Text) -and $DiskCombo.SelectedItem -and -not $script:busy)
    if ([string]::IsNullOrWhiteSpace($IsoPathBox.Text)) { $IsoInfo.Text = '请选择 Windows ISO 文件；程序会在制作前自动挂载。' }
    $DiskInfo.Text = if ($DiskCombo.Items.Count) { '列表项是整支物理磁盘；分区数量只供核对，制作时会重建整盘。' } else { '未检测到 U 盘，请连接后刷新。' }
    $DiskInfo.Visibility = if ($DiskCombo.Items.Count) { 'Collapsed' } else { 'Visible' }
    Sync-SelectionDisplay
}

function Show-FormatConfirmation([string]$ConfirmationDetails) {
    [xml]$dialogXaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="最后确认：整盘格式化" Width="580" Height="470" MinWidth="500" MinHeight="400"
        WindowStartupLocation="CenterOwner" ResizeMode="CanResizeWithGrip" ShowInTaskbar="False"
        Background="#F3F6FB" FontFamily="Microsoft YaHei UI" FontSize="13">
  <Window.Resources>
    <Style TargetType="Button">
      <Setter Property="Padding" Value="16,9"/><Setter Property="FontWeight" Value="SemiBold"/><Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Background" Value="#E7EDF6"/><Setter Property="Foreground" Value="#26354D"/><Setter Property="BorderBrush" Value="Transparent"/>
      <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border x:Name="Surface" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="2" CornerRadius="7" Padding="{TemplateBinding Padding}"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="Surface" Property="BorderBrush" Value="#3569AD"/></Trigger><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Surface" Property="Opacity" Value="0.88"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
    </Style>
  </Window.Resources>
  <Grid Margin="20">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <StackPanel Grid.Row="0" Margin="0,0,0,14">
      <DockPanel>
        <Border Width="34" Height="34" CornerRadius="17" Background="#FDE8E7" DockPanel.Dock="Left" Margin="0,0,10,0"><TextBlock Text="!" FontSize="22" FontWeight="Bold" Foreground="#C83B35" HorizontalAlignment="Center" VerticalAlignment="Center"/></Border>
        <StackPanel VerticalAlignment="Center"><TextBlock Text="最后确认" FontSize="11" FontWeight="SemiBold" Foreground="#718098"/><TextBlock Text="整盘格式化" FontSize="19" FontWeight="Bold" Foreground="#18263D"/></StackPanel>
      </DockPanel>
      <Border Background="#FFF4F2" BorderBrush="#F4D3D0" BorderThickness="1" CornerRadius="7" Padding="10" Margin="0,10,0,0"><TextBlock Text="确认后将删除目标 U 盘上的全部分区和文件。" Foreground="#A23A32" TextWrapping="Wrap"/></Border>
      <TextBlock x:Name="FormatCompatibility" Text="NTFS：部分 UEFI 固件可能不支持启动；不确定时请取消并改选 FAT32。" Foreground="#9C570D" TextWrapping="Wrap" FontSize="12.5" Margin="0,9,0,0" Visibility="Collapsed"/>
    </StackPanel>
    <Border Grid.Row="1" Background="White" BorderBrush="#D9E2EF" BorderThickness="1" CornerRadius="8" Padding="14">
      <ScrollViewer VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
        <TextBlock x:Name="ConfirmDetails" TextWrapping="Wrap" Foreground="#26354D" LineHeight="22"/>
      </ScrollViewer>
    </Border>
    <DockPanel Grid.Row="2" Margin="0,16,0,0" LastChildFill="False">
      <TextBlock Text="默认取消 · 请核对磁盘编号、名称和容量" Foreground="#69788E" VerticalAlignment="Center" DockPanel.Dock="Left"/>
      <StackPanel Orientation="Horizontal" DockPanel.Dock="Right">
        <Button x:Name="CancelFormatButton" Content="取消" IsCancel="True" IsDefault="True" MinWidth="92" Margin="0,0,9,0"/>
        <Button x:Name="ConfirmFormatButton" Content="确认格式化" IsDefault="False" MinWidth="128" Background="#C83B35" Foreground="White"/>
      </StackPanel>
    </DockPanel>
  </Grid>
</Window>
'@
    $dialog = [Windows.Markup.XamlReader]::Load((New-Object System.Xml.XmlNodeReader $dialogXaml))
    $dialog.Owner = $window
    $dialog.Icon = $window.Icon
    if ([string]$FsCombo.SelectedItem.Tag -eq 'NTFS') { $dialog.FindName('FormatCompatibility').Visibility = 'Visible' }
    $detailTextBlock = $dialog.FindName('ConfirmDetails')
    $cancelButton = $dialog.FindName('CancelFormatButton')
    $confirmButton = $dialog.FindName('ConfirmFormatButton')
    $detailTextBlock.Text = $ConfirmationDetails
    $confirmButton.Add_Click({ $dialog.DialogResult = $true })
    $cancelButton.Add_Click({ $dialog.DialogResult = $false })
    $dialog.Add_ContentRendered({ $cancelButton.Focus() | Out-Null })
    return [bool]$dialog.ShowDialog()
}

function Invoke-UiPrompt($Request) {
    $prompt = [string]$Request.Prompt
    $index = [int]$Request.Index
    $reply = ''
    if ($index -eq 4 -and $prompt -like '*编号*') {
        $disk = $DiskCombo.SelectedItem
        $fs = [string]$FsCombo.SelectedItem.Tag
        $details = "目标磁盘：磁盘 $($disk.Number)  |  $($disk.FriendlyName)  |  $([math]::Round($disk.Size/1GB,1)) GB`n文件系统：$fs`n`nISO 文件：`n$($IsoPathBox.Text)`n`n格式化后会删除这支 U 盘的所有旧分区（包括 EFI、隐藏分区和微 PE 分区），并建立一个占用全容量的 $fs 主分区。`n`n请确认已备份，并再次核对目标磁盘编号、设备名称和容量。"
        if ($script:engineState -and $script:engineState['InstallName']) { $details = "安装系统：$($script:engineState['InstallName'])`n卷标：$($script:engineState['VolumeLabel'])`n`n" + $details }
        if ($fs -eq 'NTFS') { $details += "`n`nNTFS 启动兼容性：部分 UEFI 固件可能不支持直接从 NTFS 启动。不确定时请取消并改选 FAT32。" }
        $confirmed = Show-FormatConfirmation $details
        $reply = if ($confirmed) { '1' } else { '2' }
    } elseif ($prompt -like '*安全弹出 U 盘*') {
        $details = if ($Request.Prompt -match '([A-Z]):') { "目标盘符：$($Matches[1]):\`n设备：$($DiskCombo.SelectedItem.FriendlyName)" } else { "设备：$($DiskCombo.SelectedItem.FriendlyName)" }
        $confirmed = Show-UiDialog -Title '安全弹出 U 盘' -Heading '制作已完成' -Message '现在尝试安全弹出目标 U 盘吗？' -Details $details -Hint '选择“保持连接”可稍后手动弹出。' -Kind Info -PrimaryText '安全弹出' -SecondaryText '保持连接' -DefaultPrimary $true
        $reply = if ($confirmed) { '' } else { 'Q' }
    } elseif ($prompt -like '*弹出已识别的 ISO*') {
        $confirmed = Show-UiDialog -Title '弹出 Windows ISO' -Heading '释放安装镜像' -Message '是否弹出本次制作使用的 Windows ISO？' -Details $IsoPathBox.Text -Hint '选择“保留挂载”可继续使用此镜像。' -Kind Info -PrimaryText '弹出 ISO' -SecondaryText '保留挂载' -DefaultPrimary $true
        $reply = if ($confirmed) { '' } else { 'Q' }
    } elseif ($prompt -like '*清理*保留临时文件*') {
        $confirmed = Show-UiDialog -Title '清理临时文件' -Heading '释放本次制作的临时空间' -Message '是否删除本次生成的临时 SWM 文件？' -Hint '这不会影响已制作完成的 U 盘。' -Kind Info -PrimaryText '清理文件' -SecondaryText '保留文件' -DefaultPrimary $true
        $reply = if ($confirmed) { '' } else { 'Q' }
    } else {
        # Only the known non-destructive step pauses are supplied by Start.
        if ($prompt -in @('按回车执行本步；输入 Q 退出','按回车执行本机拆分；输入 Q 退出','按回车跳过拆分并开始复制；输入 Q 退出')) { $reply = '' }
        else { $Request.Cancelled = $true; Add-UiLog ('已停止：无法识别交互请求，未自动确认：' + $prompt) }
    }
    $Request.Response = $reply
    $Request.Ready.Set() | Out-Null
}

function Confirm-StartSelection {
    $disk = $DiskCombo.SelectedItem
    $fs = [string]$FsCombo.SelectedItem.Tag
    $details = "安装镜像`n$($IsoPathBox.Text)`n`n写入U盘`n$($disk.Display)`n`n文件系统`n$fs"
    if ($fs -eq 'NTFS') { $details += "`n部分 UEFI 固件可能不支持直接从 NTFS 启动。" }
    return (Show-UiDialog -Title '开始制作' -Heading '先检查本次制作目标' -Message '程序会自动挂载并检查所选 ISO，然后开始准备流程。' -Details $details -Hint '清理磁盘前还会再次显示设备信息并单独确认。' -Kind Info -PrimaryText '继续检查' -SecondaryText '返回检查' -DefaultPrimary $false)
}

function Confirm-StopWork([switch]$Closing) {
    return (Show-UiDialog -Title $(if ($Closing) { '关闭制作工具' } else { '强制结束制作' }) -Heading '任务仍在运行' -Message '强制结束会中断当前操作并关闭程序。' -Details '如果此时正在清盘、格式化或复制，U 盘内容可能不完整，需要重新制作。自动挂载的 ISO 可能保留，可稍后在资源管理器中弹出。' -Hint '默认继续运行，避免误中断。' -Kind Warning -PrimaryText $(if ($Closing) { '强制结束并关闭' } else { '强制结束' }) -SecondaryText '继续制作' -DefaultPrimary $false)
}

function Stop-ActiveWork([switch]$Confirmed) {
    if (-not $script:busy) { return }
    if (-not $Confirmed) {
        $stop = Confirm-StopWork
        if (-not $stop) { return }
    }

    $ForceStopButton.IsEnabled = $false
    $timer.Stop()
    try { Add-UiLog '收到强制结束请求，正在停止当前命令…' } catch { }
    if ($script:engineState) {
        $script:engineState['ForceStopRequested'] = $true
        $child = $script:engineState['ActiveProcess']
        if ($child) {
            try {
                if (-not $child.HasExited) { $child.Kill(); $null = $child.WaitForExit(1500) }
            } catch { }
        }
    }
    while ($script:promptQueue.Count -gt 0) {
        $pending = $null
        if (-not $script:promptQueue.TryTake([ref]$pending)) { break }
        if ($pending.Kind -eq 'Prompt') {
            try { $pending.Response = 'Q'; $pending.Ready.Set() | Out-Null } catch { }
        }
    }
    if ($script:engine) {
        try {
            $stopTask = $script:engine.StopAsync($null, $null)
            $null = $stopTask.Wait(1800)
        } catch { }
    }
    # A blocked storage cmdlet can keep Dispose/Close/Dismount blocked too.
    # Forced termination must never wait synchronously on those operations.
    if ($script:engineState) {
        $child = $script:engineState['ActiveProcess']
        if ($child) { try { if (-not $child.HasExited) { $child.Kill() } } catch { } }
    }
    [Environment]::Exit(2)
}

function Start-Engine {
    if ($Preview) { return }
    if ($script:busy) { return }
    $isoPath = [string]$IsoPathBox.Text
    $disk = $DiskCombo.SelectedItem
    if ([string]::IsNullOrWhiteSpace($isoPath) -or -not (Test-Path -LiteralPath $isoPath -PathType Leaf) -or -not $disk) {
        Show-UiDialog -Title '无法开始制作' -Heading '请重新检查所选文件和设备' -Message 'ISO 文件不存在、无法访问，或尚未选择 U 盘。请重新选择后再开始。' -Kind Warning | Out-Null
        return
    }
    if (-not $script:workerScript) { throw '缺少后台任务资源，请重新构建程序。' }
    if ([IO.Path]::GetExtension($isoPath) -ine '.iso') { Show-UiDialog -Title '安装镜像' -Heading '文件类型不匹配' -Message '请选择 Windows ISO 映像文件。' -Details '支持的文件扩展名：.iso' -Kind Warning -PrimaryText '知道了' | Out-Null; return }
    $script:busy = $true
    $script:runSucceeded = $false; $script:runFailed = $false
    Set-RunLayout $true
    Set-StageDisplay 1
    $script:engineLog.Clear() | Out-Null
    $LogBox.Clear()
    $MainProgress.Value = 0
    $MainProgress.IsIndeterminate = $true
    $PercentText.Text = ''
    $PhaseText.Text = '正在准备'
    $LogStatus.Text = '制作中'
    $FileText.Text = '正在检查安装镜像…'; $FileProgress.Value = 0; $ElapsedText.Text = ''
    $FooterText.Text = '正在准备 ISO；请保持镜像文件和 U 盘可用。'
    Add-UiLog '正在启动后台任务，准备挂载并识别 ISO…'
    $StartButton.IsEnabled = $false
    $RefreshButton.IsEnabled = $false
    $ForceStopButton.Visibility = 'Visible'
    $ForceStopButton.IsEnabled = $true
    $script:engineState = [hashtable]::Synchronized(@{ ActiveProcess=$null; MountedByApp=$false; ForceStopRequested=$false; IsoPath=$isoPath; ExitCode=$null; CopyVerified=$false; TargetRebuildStarted=$false; LastError='' })
    $script:sessionLogDirectory = ''
    try {

    $initialState = [System.Management.Automation.Runspaces.InitialSessionState]::CreateDefault()
    $runspace = [runspacefactory]::CreateRunspace($initialState)
    $runspace.ApartmentState = 'STA'
    $runspace.ThreadOptions = 'ReuseThread'
    $runspace.Open()
    $script:engineRunspace = $runspace
    $runspace.SessionStateProxy.SetVariable('GuiPromptQueue', $script:promptQueue)
    $runspace.SessionStateProxy.SetVariable('GuiEngineState', $script:engineState)
    $runspace.SessionStateProxy.SetVariable('GuiIsoPath', $isoPath)
    $runspace.SessionStateProxy.SetVariable('GuiEngineScript', $script:engineScript)
    $runspace.SessionStateProxy.SetVariable('GuiLauncherPath', [string]$global:Win11UsbLauncherPath)
    $runspace.SessionStateProxy.SetVariable('GuiDiskNumber', [string]$disk.Number)
    $runspace.SessionStateProxy.SetVariable('GuiFileSystem', [string]$FsCombo.SelectedItem.Tag)
    $runspace.SessionStateProxy.SetVariable('GuiDiskSnapshot', $disk.PSObject.Copy())
    $script:engine = [powershell]::Create()
    $script:engine.Runspace = $runspace
    $null = $script:engine.AddScript($script:workerScript)
    $script:engineHandle = $script:engine.BeginInvoke()
    } catch {
        $script:engineState['ExitCode'] = 1
        $script:engineState['LastError'] = $_.Exception.Message
        Add-UiLog ('准备失败：' + $_.Exception.Message)
        if ($script:engine) { $script:engine.Dispose(); $script:engine = $null }
        if ($script:engineRunspace) { $script:engineRunspace.Dispose(); $script:engineRunspace = $null }
        $script:busy = $false
        Set-RunLayout $false
        $MainProgress.IsIndeterminate = $false
        $PhaseText.Text = '准备失败，请查看日志'
        Show-RunResult
    }
}

$timer = New-Object Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds(120)
function Receive-EngineMessages([switch]$All) {
    $count = 0
    while ($script:promptQueue.Count -gt 0 -and ($All -or $count -lt 500)) {
        $item = $null
        if (-not $script:promptQueue.TryTake([ref]$item)) { break }
        $count++
        switch ($item.Kind) {
            'Prompt' {
                try { Invoke-UiPrompt $item }
                catch {
                    $item.Cancelled = $true
                    try { $item.Ready.Set() | Out-Null } catch { }
                    Add-UiLog ('已停止：处理交互请求失败：' + $_.Exception.Message)
                }
            }
            'Log' { Add-UiLog ([string]$item.Text); Update-ProgressDisplay ([string]$item.Text) }
            'Inline' { $LogStatus.Text = '实时更新'; Update-ProgressDisplay ([string]$item.Text) }
        }
    }
}

function Save-SessionLog {
    try {
        $directory = if ($script:engineState -and $script:engineState['WorkRoot']) { [string]$script:engineState['WorkRoot'] } else { Join-Path $env:LOCALAPPDATA ('Win11Usb-Logs\' + [guid]::NewGuid().ToString('N')) }
        $null = [IO.Directory]::CreateDirectory($directory)
        $path = Join-Path $directory 'session.log'
        [IO.File]::WriteAllText($path,$LogBox.Text,(New-Object Text.UTF8Encoding($true)))
        return $path
    } catch { return '' }
}

function Show-RunResult {
    $result = $script:engineState
    $logPath = Save-SessionLog
    $success = $null -ne $result['ExitCode'] -and [int]$result['ExitCode'] -eq 0 -and $result['CopyVerified'] -and -not $script:runFailed
    $script:runSucceeded = [bool]$success
    $MainProgress.IsIndeterminate = $false
    $LogStatus.Text = '已结束'
    $FooterText.Text = if ($logPath) { '日志已保存：' + $logPath } else { '日志保留在窗口中，可选中并复制用于排查。' }
    if ($success) {
        $PhaseText.Text = '制作完成'; Set-StageDisplay 6 -Complete
        $ProgressLabel.Text = '制作结果'; $PercentText.Text = '已完成'; $MainProgress.Value = 100
        $details = "设备：$($DiskCombo.SelectedItem.FriendlyName)`n文件系统：$($FsCombo.SelectedItem.Tag)`n系统：$($result['InstallName'])`n卷标：$($result['VolumeLabel'])`n`n$($result['UsbResult'])`n$($result['IsoResult'])"
        Show-UiDialog -Title '制作结果' -Heading '安装 U 盘已制作完成' -Message '安装文件已复制并通过文件大小校验。安装时请在电脑启动菜单中选择 UEFI: U盘名称。' -Details $details -Hint '日志已保留，可用于排查。' -Kind Info -PrimaryText '完成' | Out-Null
    } else {
        $cancelled = -not $result['TargetRebuildStarted'] -and [string]$result['LastError'] -match '已取消|交互请求已取消|已退出'
        $PhaseText.Text = if ($cancelled) { '已取消制作' } elseif ($result['CopyVerified']) { '复制已校验，后续步骤未完成' } else { '制作未完成，请查看日志' }
        $ProgressLabel.Text = '停止时的进度'
        $status = if ($result['TargetRebuildStarted']) { '目标 U 盘已经进入重建流程，请保留日志并重新制作。' } else { '尚未清盘，目标 U 盘未被修改。' }
        $message = if ($result['CopyVerified']) { '文件复制与大小校验已通过，但后续步骤出现问题。' } else { $status }
        $details = [string]$result['LastError']
        if ($logPath) { $details += "`n`n日志：$logPath" }
        Show-UiDialog -Title '制作结果' -Heading $PhaseText.Text -Message $message -Details $details -Hint '请保留日志，核对后再重试。' -Kind $(if ($cancelled) { 'Info' } else { 'Error' }) -PrimaryText '知道了' | Out-Null
    }
}

$timer.Add_Tick({
    if ($script:processingTick -or -not $script:engine) { return }
    $script:processingTick = $true
    try {
        foreach ($progress in @($script:engine.Streams.Progress)) {
            if ($progress.RecordType -eq [System.Management.Automation.ProgressRecordType]::Completed) { continue }
            if ($progress.ParentActivityId -lt 0) {
                if ($progress.PercentComplete -ge 0) { $MainProgress.IsIndeterminate = $false; $MainProgress.Value = $progress.PercentComplete; $PercentText.Text = "$($progress.PercentComplete)%" }
                else { $MainProgress.IsIndeterminate = $true; $PercentText.Text = '' }
                if ($progress.Activity) { $PhaseText.Text = $progress.Activity }
                $ProgressLabel.Text = if ($progress.Activity -match '复制') { '复制整体' } else { '当前操作' }
                if ($progress.StatusDescription) { Update-ProgressDisplay $progress.StatusDescription }
            } else {
                if ($progress.PercentComplete -ge 0) { $FileProgress.Value = $progress.PercentComplete }
                if ($progress.StatusDescription -match '([\d.]+)%\s*\|\s*(.+)$') { $FileText.Text = '当前文件：' + $Matches[2] + ' · ' + $Matches[1] + '%' }
            }
            $LogStatus.Text = '实时更新'
        }
        $script:engine.Streams.Progress.Clear()
        Receive-EngineMessages
        if ($script:engineHandle.IsCompleted) {
            try {
                foreach ($record in $script:engine.EndInvoke($script:engineHandle)) { if ($null -ne $record) { Add-UiLog ([string]$record) } }
            } catch {
                $script:engineState['ExitCode'] = 1
                $script:engineState['LastError'] = $_.Exception.Message
                $script:runFailed = $true
                Add-UiLog ('已停止：' + $_.Exception.Message)
            }
            # EndInvoke is the producer-completion barrier. Drain again after it;
            # final success/failure messages may have arrived during the first drain.
            Receive-EngineMessages -All
            $script:engine.Dispose(); $script:engine = $null; $script:engineHandle = $null
            if ($script:engineRunspace) { $script:engineRunspace.Close(); $script:engineRunspace.Dispose(); $script:engineRunspace = $null }
            $script:busy = $false
            Set-RunLayout $false
            Show-RunResult
        }
    } finally { $script:processingTick = $false }
})
$timer.Start()

$RefreshButton.Add_Click({ Refresh-Devices })
$SelectIsoButton.Add_Click({
    $dialog = New-Object Microsoft.Win32.OpenFileDialog
    $dialog.Title = '选择 Windows 安装 ISO'
    $dialog.Filter = 'Windows ISO 映像 (*.iso)|*.iso|所有文件 (*.*)|*.*'
    $dialog.CheckFileExists = $true
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog($window)) {
        $IsoPathBox.Text = $dialog.FileName
        $IsoInfo.Text = '开始制作时自动挂载并识别系统版本。'
        Sync-SelectionDisplay
        $StartButton.IsEnabled = [bool]($DiskCombo.SelectedItem -and -not $script:busy)
    }
})
$DiskCombo.Add_SelectionChanged({ if ($DiskCombo.SelectedItem) { $DiskInfo.Text = '请核对磁盘编号、设备名称和容量。'; Sync-SelectionDisplay } })
$Fat32Option.Add_Checked({ $FsCombo.SelectedIndex = 0; Sync-SelectionDisplay })
$NtfsOption.Add_Checked({ $FsCombo.SelectedIndex = 1; Sync-SelectionDisplay })
$ForceStopButton.Add_Click({
    if ($Preview) {
        Set-RunLayout $false
        Refresh-Devices
        Set-StageDisplay 0
        $PhaseText.Text = '准备就绪'; $ProgressLabel.Text = '当前操作'; $PercentText.Text = '0.0%'
        $MainProgress.Value = 0; $FileProgress.Value = 0; $FileText.Text = '等待开始制作'; $ElapsedText.Text = ''
        $LogStatus.Text = '预览已结束'
        $FooterText.Text = '预览模式 · 不会修改磁盘。'
    } else { Stop-ActiveWork }
})
$StartButton.Add_Click({
    if ($Preview) { Show-RunningPreview; return }
    if (Confirm-StartSelection) { Start-Engine }
})
$window.Add_Closing({
    param($sender,$eventArgs)
    if ($script:busy) {
        $eventArgs.Cancel = $true
        $choice = Confirm-StopWork -Closing
        if ($choice) { Stop-ActiveWork -Confirmed }
    }
})
$window.Add_Closed({
    $timer.Stop()
    if ($script:engineRunspace) {
        try { if ($script:engineRunspace.RunspaceStateInfo.State -ne 'Closed') { $script:engineRunspace.Close() } } catch { }
        try { $script:engineRunspace.Dispose() } catch { }
        $script:engineRunspace = $null
    }
    if ($script:promptQueue -and -not $script:promptQueue.IsAddingCompleted) { try { $script:promptQueue.CompleteAdding() } catch { } }
    if ($script:promptQueue) { try { $script:promptQueue.Dispose() } catch { } }
})
Refresh-Devices
Set-RunLayout $false
Set-StageDisplay 0
$window.Title = if ($Preview) { 'Windows 安装 U 盘制作工具 - 界面预览' } else { $window.Title }
$FooterText.Text = if ($Preview) { '界面预览模式：不会启动制作流程，也不会修改磁盘。' } else { $FooterText.Text }
if ($PreviewRunning) { Show-RunningPreview }
$window.Add_ContentRendered({ if ($script:showingRun) { $LogBox.ScrollToEnd() } })
if (-not $PreviewRunning) { $LogBox.Text = '等待开始制作。操作日志将在这里实时显示。' }
$null = $window.ShowDialog()

