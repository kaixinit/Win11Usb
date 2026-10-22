# 程序结构

当前为 0.1.0-beta.3 原生界面预发布候选。用户已在原报错电脑完成一次 FAT32 大 WIM 制作并反馈全程未报毒；旧 beta.2 的 EXE 已撤回。验证边界见 VALIDATION.md。

Program.cs 直接创建 C# NativeWindow 与 WPF Application。主界面、普通提示和最终格式化确认的 XAML 从 beta.2 原样提取为 src/Ui 下的三个资源。启动和设备刷新不初始化 PowerShell；DiskReader 通过 System.Management 只读查询 MSFT_Disk、MSFT_Partition 和 Win32_DiskDrive，取得与原来相同的整盘身份字段。设备查询在后台线程执行。

管理员权限通过内嵌 requireAdministrator / uiAccess=false 应用清单向 Windows 声明，不使用启动器自我重启、隐藏 PowerShell 提权或执行策略覆盖。构建检查器只将 EXE 作为数据读取，核验清单和资源，不在构建阶段运行管理员程序。

点击开始、完成第一轮检查确认后，才创建 WorkflowRunner。它在后台线程初始化同进程 STA Runspace，把 ISO 路径、文件系统、启动程序路径、队列及设备选择快照作为变量传入，执行原有 Worker.ps1 与 Engine.ps1；两者内容哈希保持 beta.2 不变。快照在提交后台任务前复制。PowerShell 自带的安全集成、日志和策略检查保持启用；没有关闭 AMSI、修改 Defender、添加排除项或覆盖语言模式。

Worker 将日志与提示请求写入 BlockingCollection。WorkflowRunner 把请求转换为 C# 模型，NativeWindow 用 DispatcherTimer 按批处理，防止一次过多消息占住界面。后台 EndInvoke 完成后再收取尾部消息，依据 ExitCode 和 CopyVerified 判断结果，随后释放后台资源并保存 session.log。

前台只提供用户选择。后台选择磁盘后重新比对 Number、Size、FriendlyName、Path、SerialNumber 和 PNP 身份快照。清盘及后续步骤仍进行各自核验。源 ISO 的卷、盘符和映像身份也会复核。

未知提示请求不会自动按回车。整盘确认默认取消；只有明确确认才返回引擎所需的确认值。可选清理和弹出分别映射“执行”或 Q 跳过。

强制结束先标记取消、终止已跟踪的原生命令，限时请求停止后台，然后终止当前进程。此路径不等待可能阻塞的 Runspace.Close、Dispose 或 ISO 卸载。强制退出的提示会说明未完成 U 盘与可能保留的 ISO；它不是可恢复暂停。

测试通过替换存储、映像和原生命令边界运行真实 Worker 与引擎入口。测试不等同于真实文件系统格式化、硬件安全弹出或 UEFI 启动验证。

测试还包括 C# 界面初始化时未加载 System.Management.Automation、原生 UI 预览隔离、提示桥接、双进度、末尾消息和生产强制退出函数。测试宿主在普通用户权限运行同一份 C# 代码与 XAML；它不等同于正式 EXE 的管理员 UAC 启动验证。Win11Usb.Gui.ps1 保留为脚本参考及既有回归用，不再内嵌到候选 EXE；其旧的隐藏 PowerShell 提权分支已移除。
