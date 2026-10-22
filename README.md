<div align="center">
  <img src="https://raw.githubusercontent.com/kaixinit/Win11Usb/main/assets/usb-eject-source.png" width="80" alt="Win11Usb 应用图标">
  <h1>Win11Usb</h1>
  <p><strong>Windows 安装 U 盘制作工具</strong></p>
  <p>选好 ISO 和 U 盘，在一个窗口完成制作。<br>全容量 FAT32 · 大 WIM 自动拆分 · 本地离线制作 · 实时进度</p>
  <p>
    <a href="https://github.com/kaixinit/Win11Usb/releases/tag/v0.1.0-beta.3"><img src="https://img.shields.io/badge/Pre--release-v0.1.0--beta.3-2563eb?style=flat-square" alt="v0.1.0-beta.3 预发布"></a>
    <img src="https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-0078d4?style=flat-square" alt="面向 Windows 10 / 11 x64">
    <a href="https://github.com/kaixinit/Win11Usb/actions/workflows/windows.yml"><img src="https://github.com/kaixinit/Win11Usb/actions/workflows/windows.yml/badge.svg?branch=main" alt="Windows 构建与回归测试"></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-16a34a?style=flat-square" alt="项目原创代码采用 MIT 许可证"></a>
  </p>
  <p>
    <a href="https://github.com/kaixinit/Win11Usb/releases/download/v0.1.0-beta.3/Win11Usb-v0.1.0-beta.3-windows-x64.zip"><strong>下载 Windows x64 · beta.3</strong></a>
    &nbsp;·&nbsp; <a href="https://github.com/kaixinit/Win11Usb/releases">全部版本</a>
    &nbsp;·&nbsp; <a href="README.en.md">English</a>
  </p>
</div>

> **制作会清空所选 U 盘的全部分区和文件。** 请先备份，并在最终确认框核对磁盘编号、名称与容量。当前为预发布版本。

## 制作过程，看得见

从选择镜像，到复制、校验与清理，都在同一个窗口。六阶段导航、复制整体进度、当前文件进度和滚动日志同时显示。

![Win11Usb 制作界面：六阶段导航、整体与当前文件进度、实时日志](docs/screenshots/running.jpg)

*截图来自实际程序的预览模式，使用示例设备和模拟进度，不作为实机制作或启动成功的证明。*

<details>
<summary>更多界面：选择镜像与最终格式化确认</summary>

**选择本地 ISO 和要写入的整支 U 盘**

![选择 ISO、写入 U 盘和文件系统，示例数据](docs/screenshots/ready.jpg)

**清盘前单独确认，默认取消**

![NTFS 最终确认：磁盘身份、清盘范围和启动兼容性提醒，示例数据](docs/screenshots/format-ntfs.jpg)

</details>

## 为什么选择 Win11Usb

| 特色 | 能帮你做什么 |
| --- | --- |
| **大容量 U 盘，也能重建为全容量 FAT32** | 支持将原 NTFS 格式或多分区 PE 盘清空后，重建为一个占用可用容量的 FAT32 分区。需要时调用随包提供的兼容格式化工具，不把大盘缩到约 31 GB。 |
| **大 WIM，按需自动拆分** | install.wim 超过 4,294,967,295 字节时，先在本机拆成 SWM 再复制。选择 NTFS 也沿用相同规则。 |
| **打开即用，本地制作** | 单文件 EXE，无需额外安装运行库。选择已有的 Windows ISO，程序自动挂载并识别映像；制作无需联网。 |
| **过程清晰，结果可查** | 六阶段导航、整体与当前文件双进度、实时日志；复制后检查文件大小，结束时可清理临时文件、安全弹出 U 盘与 ISO。 |

全容量 FAT32 按当前流程支持不超过 2 TiB 的 USB 磁盘；大于 32 GB 的兼容格式化分支仍待补充当前版本的实机测试。**这是清空后重建，不是保留文件的无损转换。**

制作前会排除系统盘、启动盘、离线和只读磁盘，并复核目标身份、分区布局及源文件位置。ISO、程序或临时文件位于目标盘时会停止。这些检查不能替代备份和人工核对。

## 下载

普通用户选择 **Windows x64 运行包**，解压后打开 Win11Usb.exe 即可。

| 文件 | 用途 |
| --- | --- |
| **[下载 Windows x64 运行包](https://github.com/kaixinit/Win11Usb/releases/download/v0.1.0-beta.3/Win11Usb-v0.1.0-beta.3-windows-x64.zip)** | 单文件 EXE、使用说明、第三方许可与对应源码，无需安装。 |
| [下载项目源码](https://github.com/kaixinit/Win11Usb/releases/download/v0.1.0-beta.3/Win11Usb-v0.1.0-beta.3-source.zip) | 项目源码、构建脚本、测试与 GitHub 配置。 |
| [SHA-256 校验清单](https://github.com/kaixinit/Win11Usb/releases/download/v0.1.0-beta.3/SHA256SUMS.txt) | 核对两个 ZIP；运行包内另有 EXE 清单。 |

[查看 beta.3 发布说明](https://github.com/kaixinit/Win11Usb/releases/tag/v0.1.0-beta.3) · [使用与常见问题](docs/FAQ.md)

## 开始制作

准备一个本地 Windows 10 / 11 x64 安装 ISO，以及一支**已备份、容量足够且至少 8 GiB** 的 USB 磁盘。

1. 从发布包取出 Win11Usb.exe，运行并确认 Windows 管理员权限提示。
2. 选择本地 Windows ISO，选择“写入U盘”中的目标整盘。
3. 选择 FAT32 或 NTFS。通常优先选择 FAT32；部分 UEFI 固件可能不支持直接从 NTFS 启动。
4. 点击“开始制作”，核对检查信息，再单独确认整盘格式化。默认取消。
5. 程序识别映像、重建分区、按需拆分、复制并检查文件大小。结束后可清理临时 SWM，选择是否安全弹出 U 盘与 ISO。

NTFS 也沿用相同拆分规则：install.wim 大于 **4,294,967,295 字节**才拆分。DISM 使用 /FileSize:4000，生成多少个分卷由实际内容决定，每个生成文件还会再检查大小。所有分卷复制到 sources 目录。FAT32 与 SWM 的背景见 [Microsoft 安装介质说明](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/install-windows-from-a-usb-flash-drive?view=windows-11) 和 [映像拆分说明](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/split-a-windows-image--wim--file-to-span-across-multiple-dvds?view=windows-11)。

<details>
<summary>制作前核对什么？环境与兼容性有哪些限制？</summary>

程序排除系统盘、启动盘、离线和只读磁盘；核对界面选择快照、设备身份、分区布局、源 ISO、运行程序和临时目录所在磁盘。ISO 或程序放在准备清空的 U 盘上时会停止。无法确认身份、格式化结果或复制大小时，也会停止后续步骤。

这些检查不能替代备份与人工核对。进度为阶段进度；复制整体按字节估算。最终校验为**文件大小校验**，没有宣称对每个安装文件进行密码学哈希校验。

### 环境与限制

- 面向 Windows 10 / 11 x64，依赖系统自带 Windows PowerShell 5.1、WPF、Storage、DISM 与 robocopy。
- USB 容量至少 8 GiB，本流程重建 MBR 单分区，上限 2 TiB；实际所需容量由 ISO 大小决定。
- 安装介质需要 efi/boot/bootx64.efi、sources/boot.wim 和 setup.exe，主要面向 x64 UEFI 安装。
- 识别和卷标以安装映像索引 1 为依据；多版本 ISO 不表示只安装此版本。发行版本可能用 ISO 文件名辅助判断，不能验证错误重命名。
- 不是多系统引导工具；不下载 Windows、不提供激活，也不承诺适用于所有固件。
- 强制结束会关闭程序，U 盘可能需要重新制作；自动挂载的 ISO 可能保留。
- 当前未签名。Defender 单次文件扫描、模拟测试与实机启动验证是不同的检查，见 [验证记录](docs/VALIDATION.md)。

</details>

<details>
<summary>开发者：从源码构建与预览</summary>

在仓库根目录的 Windows PowerShell 5.1 中运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\build\Build-Win11Usb.ps1
powershell.exe -NoProfile -STA -ExecutionPolicy RemoteSigned -File .\tests\Run-Tests.ps1
```

下载源码包后，在仓库根目录构建。输出为 dist/Win11Usb.exe。使用 Windows 自带 .NET Framework C# 编译器，构建不需要 PS2EXE、Node.js 或 NuGet。C# 直接创建界面，通过标准应用清单请求管理员权限；用户确认开始后才在同进程后台托管制作 Runspace。制作时仍会调用系统 DISM、robocopy 等命令。

```powershell
.\dist\Win11Usb.exe --self-check
.\dist\Win11Usb.exe --preview
.\dist\Win11Usb.exe --preview-running
.\dist\Win11Usb.exe --preview-empty
```

预览使用示例数据，不挂载 ISO、不运行制作引擎、不修改磁盘。正式制作中的 FAT32 兼容工具会解压到本次本地工作目录。

[架构说明](docs/ARCHITECTURE.md) · [贡献指南](https://github.com/kaixinit/Win11Usb/blob/main/CONTRIBUTING.md)

</details>

## 当前验证范围

**beta.3 已通过 GitHub Windows 构建和 11 组回归测试。** 用户提供的实机日志记录了 FAT32 制作完成、复制大小校验通过、临时文件清理及 USB/ISO 弹出成功，并反馈全程未报毒。

当前实机证据覆盖一支 14.6 GB Lenovo U 盘、FAT32、大 WIM 拆分和安全弹出；从 U 盘进入安装程序、NTFS 实机、更多硬件及 DPI 仍需补测。先按 Pre-release 使用，不把一次成功等同于完整兼容认证。

EXE 当前未签名。旧 beta.1 / beta.2 已撤回；beta.3 的本地扫描与一次实机反馈不保证所有安全软件均无检测。[完整验证记录与脱敏日志](docs/VALIDATION.md) · [安全说明](https://github.com/kaixinit/Win11Usb/blob/main/SECURITY.md)

## 反馈与许可证

发现问题或有功能建议，欢迎[提交 Issue](https://github.com/kaixinit/Win11Usb/issues)。请用模板提供版本、系统构建号、出错阶段及脱敏日志。日志可能含个人路径、磁盘信息，公开前请检查。也欢迎分享 U 盘与固件兼容性结果，帮助补齐测试矩阵。

如果 Win11Usb 对你有帮助，欢迎点一个 **Star**，让更多需要制作安装 U 盘的人找到它。

项目原创代码按 [MIT](LICENSE) 许可。独立运行的 Ridgecrop fat32format 及其原始源码保留 GPL 声明，不改标为 MIT；配套源码、许可与哈希见 [第三方说明](THIRD_PARTY_NOTICES.md)。图标已替换为本项目原创生成资产，并提供来源记录。
