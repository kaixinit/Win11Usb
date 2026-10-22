# Win11Usb 0.1.0-beta.3

把本地 Windows ISO 制作为安装 U 盘的便携窗口工具。此版本建议先标记为 **Pre-release**。

beta.3 让 C# 直接创建原有 WPF 界面，使用 Windows 标准管理员权限清单。设备刷新不初始化 PowerShell，用户确认开始后才在后台加载原有制作引擎；保留界面布局、整盘复核、4 GiB 拆分规则、双进度和弹窗确认。

旧 beta.2 曾发生 Defender 启动检测，旧 beta.1 / beta.2 包已撤回。此次用户在原报错电脑正常启动，并完成 Lenovo 14.6 GB U 盘的 FAT32 大 WIM 制作，报告全程未报毒。日志显示 7.48 GiB WIM 拆为两个 SWM、复制大小校验通过、临时文件清理及 USB/ISO 弹出成功。本地 11 组回归、文件扫描及原生界面联动检查通过；证据和限制见包内 docs/VALIDATION.md。未获得 Microsoft 对旧版的误报认定，不保证所有机器无检测。

下载材料：

- Windows x64 运行包：单文件 EXE、使用说明、第三方许可及对应源码。
- 项目源码包：源代码、构建、11 组测试、GitHub Actions 与 Issue 模板。
- SHA256SUMS.txt：核对两个 ZIP；运行包内另有 EXE 的校验清单。

制作会清空所选整支 U 盘。NTFS 在部分 UEFI 固件上不能直接启动；当前未提供数字签名。实际 UEFI 引导、NTFS 与更多硬件/DPI 仍待验证，具体范围见验证记录。

本项目原创代码采用 MIT。独立 FAT32 工具保留 GPL 声明，原始对应源码随包提供；矢量图标保留 Apache-2.0 声明。详情见第三方说明。
