# 程序结构

## 当前原生版：C++ / Win32

`native/` 是 `0.2.0-preview.14` 原生预发布的源码主路径。应用采用静态 C++ 运行库和系统 MSVCRT，分发一个 x64 GUI EXE，不加载 .NET / WPF、PowerShell、WMI / CIM 或 WebView。原有 beta.3 源码与发布资产保留；两条版本线使用独立标签和文件名。

`main.cpp` 提供正常窗口、只读诊断和明确标注的示例预览入口。`ui.cpp` 使用 Win32 控件与 GDI+ 绘制沿用 beta.3 的布局、图标、日志和双进度；`dialog.cpp` / `dialog_specs.cpp` 集中处理弹窗及按钮默认项。高频后台消息约每 100 ms 合并，日志批量追加，进度只使所属区域失效，避免复制大量小文件时反复重画整窗。

`workflow.cpp` 编排六阶段，并经 `Backend` 边界调用实际实现；`backend.cpp` 负责管理员制作。`disk.cpp` 通过 SetupAPI、存储 IOCTL 与设备树读取 USB 物理磁盘，不依赖盘符枚举。选择快照和写入前复核比较设备编号、路径、PnP、序列号、型号、容量、扇区与布局，源 ISO、程序和临时目录不能位于目标 U 盘。无法确认完整系统 / 启动盘映射时禁止写盘。

准备阶段通过系统 VirtDisk API 只读挂载手选 ISO；`wim.cpp` 使用 WIMGAPI 读取索引 1 元数据，并在清盘前查询 DISM 能力和映像。`workspace.cpp` 负责本次任务目录、可写 scratch、路径实际解析、空间检查和限定范围的 SWM 清理。清盘与新分区由 `disk_rebuild.cpp` 编排：删除旧布局后单独复核身份，初始化空 MBR，连续确认新签名及零个有效分区后才建立全容量主分区，再核验卷 GUID 与物理范围。`postformat.cpp` 等待卷和文件系统识别就绪。

FAT32 使用嵌入的独立 x64 GPL helper；NTFS 使用 Windows 自带 `format.com`。两种文件系统均保持同一拆分规则：`install.wim` 大于 4,294,967,295 字节时使用系统 DISM `/FileSize:4000` 生成 SWM，逐项检查分卷大小、编号及集合信息。`files.cpp` 使用 `CopyFileExW` 复制，刷新缓存后逐文件比较大小和内容，并复核关键 x64 UEFI 文件；校验不是介质寿命或坏块检测。

拆分、复制和校验依次自动继续；整盘格式化必须明确确认且默认取消。校验通过后分别询问本次 SWM 清理、USB 安全弹出、ISO 卸载。`eject.cpp` 按 SDK 设备类定义识别节点，核对独立 USB 存储边界、唯一所选磁盘子树和移除能力，再请求系统安全弹出；拒绝原因单独显示，不把“复制校验通过”误写成“USB 已弹出”。`eject_report.cpp` 只读取并匿名展示设备链及规划结果，不锁卷、不卸载、不提交移除请求。

`process.cpp` 直接以固定绝对路径及 Windows argv 引号规则调用必要的原生命令，不经过 PowerShell 或 `cmd.exe`。子进程纳入本次任务的 Job Object，超时、取消及异常均回收句柄和已跟踪子进程；复制回调先让系统调用取消收尾再报告异常。后台结束前统一释放文件 / 目录引用，持续日志保存在本次任务目录。强制结束是终止当前任务，不是可恢复暂停。

只读版本由 `WIN11USB_READONLY` 在编译时排除制作后端、命令执行器及实际弹出入口。它支持设备识别、示例预览及诊断；不会挂载 ISO、格式化或复制。完整版通过内嵌管理员清单请求权限，无自重启启动器。单文件指分发形态：运行制作时仍创建日志、临时 SWM，并提取 FAT32 helper 与对应源码 / 许可。Windows 的 GDI+、存储驱动、VirtDisk、WIMGAPI、DISM 等相应系统组件仍须可用。

标准自动化覆盖生产共用函数和普通文件，不写物理盘。UI 测试使用生产界面，PE 检查核验 x64、无 CLR、系统 DLL 导入及内嵌许可。用户在收到 preview.14 后提供的一次 Kingston 全容量 FAT32 制作日志已记录 2 个 SWM、1,067 个复制文件及内容校验、临时清理、USB 和 ISO 弹出成功；日志没有内载版本 / EXE 哈希。该配置的成功不能外推 NTFS、其他设备或实际 UEFI 启动。当前证据、限制和构建步骤见 [原生说明](../native/README.md) 与 [验收记录](../native/ACCEPTANCE.md)。

## 历史 beta.3：C# / WPF 与同进程 PowerShell

以下保留 `0.1.0-beta.3` 的架构说明，供旧版维护与行为对照，不是当前 C++ 版的运行依赖。用户曾在原报错电脑完成 beta.3 的一次 FAT32 大 WIM 制作并反馈全程未报毒；旧 beta.2 的 EXE 已撤回。历史验证边界见 [VALIDATION.md](VALIDATION.md)。

Program.cs 直接创建 C# NativeWindow 与 WPF Application。主界面、普通提示和最终格式化确认的 XAML 从 beta.2 原样提取为 src/Ui 下的三个资源。启动和设备刷新不初始化 PowerShell；DiskReader 通过 System.Management 只读查询 MSFT_Disk、MSFT_Partition 和 Win32_DiskDrive，取得与原来相同的整盘身份字段。设备查询在后台线程执行。

管理员权限通过内嵌 requireAdministrator / uiAccess=false 应用清单向 Windows 声明，不使用启动器自我重启、隐藏 PowerShell 提权或执行策略覆盖。构建检查器只将 EXE 作为数据读取，核验清单和资源，不在构建阶段运行管理员程序。

点击开始、完成第一轮检查确认后，才创建 WorkflowRunner。它在后台线程初始化同进程 STA Runspace，把 ISO 路径、文件系统、启动程序路径、队列及设备选择快照作为变量传入，执行原有 Worker.ps1 与 Engine.ps1；两者内容哈希保持 beta.2 不变。快照在提交后台任务前复制。PowerShell 自带的安全集成、日志和策略检查保持启用；没有关闭 AMSI、修改 Defender、添加排除项或覆盖语言模式。

Worker 将日志与提示请求写入 BlockingCollection。WorkflowRunner 把请求转换为 C# 模型，NativeWindow 用 DispatcherTimer 按批处理，防止一次过多消息占住界面。后台 EndInvoke 完成后再收取尾部消息，依据 ExitCode 和 CopyVerified 判断结果，随后释放后台资源并保存 session.log。

前台只提供用户选择。后台选择磁盘后重新比对 Number、Size、FriendlyName、Path、SerialNumber 和 PNP 身份快照。清盘及后续步骤仍进行各自核验。源 ISO 的卷、盘符和映像身份也会复核。

未知提示请求不会自动按回车。整盘确认默认取消；只有明确确认才返回引擎所需的确认值。可选清理和弹出分别映射“执行”或 Q 跳过。

强制结束先标记取消、终止已跟踪的原生命令，限时请求停止后台，然后终止当前进程。此路径不等待可能阻塞的 Runspace.Close、Dispose 或 ISO 卸载。强制退出的提示会说明未完成 U 盘与可能保留的 ISO；它不是可恢复暂停。

测试通过替换存储、映像和原生命令边界运行真实 Worker 与引擎入口。测试不等同于真实文件系统格式化、硬件安全弹出或 UEFI 启动验证。

测试还包括 C# 界面初始化时未加载 System.Management.Automation、原生 UI 预览隔离、提示桥接、双进度、末尾消息和生产强制退出函数。测试宿主在普通用户权限运行同一份 C# 代码与 XAML；它不等同于正式 EXE 的管理员 UAC 启动验证。Win11Usb.Gui.ps1 保留为脚本参考及既有回归用，不再内嵌到候选 EXE；其旧的隐藏 PowerShell 提权分支已移除。
