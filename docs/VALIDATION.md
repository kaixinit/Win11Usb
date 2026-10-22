# 0.1.0-beta.3 验证记录

更新日期：2026-10-22。状态：本地 Pre-release 材料准备，不代表已经发布到 GitHub。用户已在原报毒电脑完成一次 FAT32 大 WIM 制作，反馈全程未报毒，并提供完整日志与完成截图。此结论限于下述证据范围。

## 文件与保留范围

- 产品版本：0.1.0-beta.3；EXE 未签名。
- EXE SHA-256：`35BB091183070F495429611641C596289F692E055DCAE9D83B293B66AE7AADEB`。
- Worker SHA-256：`0CDC35F8BD1EFB63289F6F51ABD42C2AEDCDFEB127AD2F569FD8A919642BEFD1`。
- Engine SHA-256：`5073298B6853FBDA2AD0DF8525D05BC838B93F5FC40CFDBE830E0C6ACB353770`。

运行包采用用户测试链接所对应的同一份本地 EXE，未重新编译或修改字节。远端实际文件哈希没有独立核验，实机结论属于用户反馈与日志证据。

主界面、普通弹窗和格式化确认的三个 XAML 与 beta.2 提取前原文逐字一致；Worker 与 Engine 字节未改。保留整盘身份复核、自动挂载、4,294,967,295 字节拆分阈值、NTFS 相同拆分规则、复制大小校验和可选清理/弹出。C# 直接启动 WPF，设备刷新使用只读 WMI；首次确认开始后才在后台初始化 PowerShell。标准应用清单为 requireAdministrator / uiAccess=false；不使用隐藏 PowerShell 提权、执行策略覆盖或语言模式覆盖。

## 本地自动化检查

通过 tests/Run-Tests.ps1 执行 11 组检查。存储、ISO 挂载、映像及复制边界使用模拟，本地测试不会格式化物理磁盘。

| 测试组 | 范围 |
| --- | --- |
| ImagePlan | 阈值上下界、直接复制/拆分、超限 SWM 和混合映像拒绝、连续编号 |
| ImageIdentity | 39 个 Windows 10/11、LTSC、版本/卷标与构建冲突样例 |
| Storage | 多分区整盘重建、容量、空 MBR、错误状态、身份变化与源/系统盘保护 |
| Eject | USBSTOR/UASP 与设备拓扑、身份变化、拒绝结果、ISO 弹出输出模拟 |
| NativeTools | 无磁盘命令测试进程、标准输入和退出码；校验辅助工具及只执行其 /? |
| Workflow | 原 Worker/Engine 的 17 个完整模拟制作场景 |
| Prompts | 确认/取消、未知请求拒绝、NTFS 提示、结果分类和末尾日志 |
| Display | 脚本参考界面的映射、双进度、控件锁定与预览隔离回归 |
| ForceExit | 生产 C# 强制退出函数，独立测试进程退出码 2，跟踪子进程结束 |
| NativeWindow | 界面创建时不加载 PowerShell、原生选项/进度/默认取消、队列桥接与取消 |
| NativeWorkflow | 生产 C# 桥接、原 Worker/Engine 与原生提示/最终结果显示的 17 个集成模拟场景 |

17 个场景：DirectFAT32、SplitFAT32、SplitNTFS、CancelFormat、CorruptCopy、CopyFailure、MissingBoot、BadImage、MountFailure、AlreadyMounted、ExistingMountFailure、EjectVeto、KeepMedia、KeepSwm、CleanupFailure、NoMatchingIso、UnknownPrompt。

原生检查还覆盖开始前复制设备快照、1001 条尾部消息收取、长确认文本、NTFS 固定提醒、未知请求取消、限时停止。测试原始结果留在 tests/artifacts，不进入源码包。发布源码包解压到含空格的干净目录后重新构建，全部 11 组测试通过；运行包与源码包的必要文件、文档链接、脱敏和内外 SHA-256 清单均已核对。未宣称不同构建的 EXE 字节完全一致。

## 实际窗口检查

使用同源码、同 XAML 的普通权限测试宿主，检查 ISO 选择打开/取消、NTFS 选项、阶段 4、68% / 36.9% 双进度、停止预览后恢复控件和保留日志、完整文件名、最终确认的固定提醒与按钮、取消按钮、主窗口关闭后进程退出。格式化弹窗检查无制作后台，没有实际格式化。

README 配图为示例数据的实际预览窗口，不当作实机制作成功证明。未全面覆盖键盘、辅助技术、Windows 10 和 DPI 矩阵。

## 用户实机：FAT32、大 WIM 与安全弹出

收到反馈日期：2026-10-22。用户先确认原报错电脑可正常打开 beta.3，随后报告“制作完成，全程未报毒”，并提供日志和完成截图。公开的 [完整脱敏日志](validation/2026-10-22-fat32-large-wim.txt) 已移除用户名、个人 ISO 目录和任务目录标识；没有公开原始截图中的个人路径。

| 项目 | 日志结果 |
| --- | --- |
| 设备 | Lenovo Lenovo K110，界面显示 14.6 GB |
| 系统识别 | Windows 11 教育版 26H2；映像版本 10.0.26300.9457；卷标 WIN11_26H2 |
| 分区 | 旧分区删除，接受空 MBR，重建为单个全容量 FAT32 分区；日志显示 14.65 GB |
| WIM | 8,036,271,417 字节，超过阈值；DISM 成功生成两个 SWM |
| 复制 | 普通安装文件与 SWM 两次复制均结束，复制文件大小一致 |
| 引导文件 | efi/boot/bootx64.efi 已就绪 |
| 结束 | 临时 SWM 已清理，USB 与 ISO 均成功弹出 |
| 安全软件 | 用户报告启动及完整制作期间未报毒；未取得该电脑的 Defender 事件或定义版本 |

日志中的 v19 是内部制作引擎脚本版本；窗口 EXE 产品版本为 beta.3。日志不证明已从 U 盘引导或完成 Windows 安装，也不提供逐文件密码学哈希。原始日志 SHA-256：`4997A11DF3D048B48F160D473EC2449F55327A69354DE8C46B8095E2C87C60D7`；脱敏日志 SHA-256：`99C2789E89D5BA2D726D3C91BBB78B448F9447C89AEEEA8796B70C14E5766907`。

## Defender 与历史检测

2026-10-21，对上述 beta.3 EXE 执行 MpCmdRun ScanType 3，明确输出 found no threats，退出码 0；引擎 1.1.26090.9、安全情报 1.461.135.0，实时防护 True。发布准备时再次扫描的状态见 [机器可读记录](validation/release-0.1.0-beta.3.json)。

旧 beta.2 曾在另一台电脑主界面出现前被检测为 Behavior:Win32/DefenseEvasion.A!ml，旧 EXE SHA-256 为 `546BB7DD4DB694BE0E26FDCD43D76A5CB2587EB6B2FC22F9A7EE87D6AE2FFE4F`。旧 beta.1 / beta.2 包已撤回并留在本地归档，未进入本次发布包。整改后同一用户报告启动及上述制作通过；未得到 Microsoft 对旧版的误报认定，未证明具体哪项实现单独触发了检测。

没有为测试关闭防护、添加排除项、混淆或加密脚本，也未主动向外部上传样本。一次排除项查询返回拒绝访问，因此未以它证明本机没有既存排除项。文件扫描未检出和一次用户实测不保证其他电脑、其他安全软件或未来安全情报的结论。出现新检测应保留隔离并按 SECURITY.md 反馈。

## 仍待验证

- 从 USB 实际进入 Windows 安装程序与安装完成；NTFS 固件兼容性。
- NTFS 实机、大于 32 GB FAT32 兼容工具分支、小 WIM 实机、多分区 PE 盘的当前版本复测。
- Windows 10、100%/125%/150% DPI、小工作区与完整辅助技术矩阵。
- UAC 取消、真实存储驱动阻塞和热拔插过程的中断恢复。
- GitHub Actions 远程运行；目前仅准备配置并核对固定 action 提交存在，未运行远程工作流。

当前适合先作为 Pre-release 候选评审，不作全硬件兼容或安全认证承诺。尚未创建 GitHub 仓库、上传代码或发布 Release。
