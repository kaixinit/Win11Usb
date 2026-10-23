# Win11Usb C++ / Win32 原生迁移验证版

目标：沿用现有单窗口操作顺序、蓝白界面、六步进度、日志、整盘确认和制作规则，去除应用运行时的 .NET、WPF、PowerShell、WMI/CIM 依赖。发布的 beta.3 保持不变。当前原生版本为 `0.2.0-preview.14`。**Kingston 全容量 FAT32、内容校验及 USB/ISO 弹出已收到一次完整实机通过日志；实际 UEFI 启动及其他设备/NTFS 仍待验收，不应据此直接替换正式下载。**

preview.14 修正 USB 安全弹出的设备类识别：旧代码误把 `GUID_DEVCLASS_SYSTEM` 当成 USB 类，导致实际 USB 存储父节点在规划时被拒绝。现在按 SDK 定义分类，System 类继续拒绝；保留独立 USB 边界、唯一所选磁盘、能力和子树范围核验。新增从原始 GUID 经生产分类到规划的测试，以及 `--eject-report` 只读诊断；窗口“导出诊断”也附带匿名设备链与规划报告。814 项主测试和 148 项普通文件 FAT32 断言（合计 962 项）、生产 UI 的 5,000 条日志与 10,000 次进度更新测试通过。两个 EXE 的 PE/依赖检查通过，无 CLR 或新增运行依赖；实时保护开启时，Defender 1.461.155.0 文件扫描未检出。修复前的未标版本日志在 USB 规划阶段失败；用户收到 preview.14 后新提供的日志已记录全流程完成，包括 2 个 SWM、1,067 个文件复制及内容校验、临时清理、USB 安全弹出和 ISO 弹出，无失败或警告。新日志未内载版本/EXE 哈希，结论限于该次配置，见 [安全弹出设备类修正报告](USB-EJECT-preview14.md)。

preview.13 按用户要求将默认窗口收紧为 840×760 DIP，保留底部完整操作区并停止随长内容自动增高；补齐首次 NTFS 确认与强制结束提示。生产弹窗规格集中共享，76 项新增规格断言及生产 UI 回归通过。实际只读窗口截图与人工检查中断范围见 [最终界面复核](UI-CHECK-preview13.md)。

preview.12 以 GitHub beta.3 的 XAML、原始矢量图标和交互代码恢复主界面与弹窗：拆分/复制/校验自动继续，恢复明确的清理/弹出按钮和成功结果窗口；保留 C++ 内容校验、句柄释放、防闪烁与安全弹出修复。930 项流程/普通文件 FAT32 断言及生产 UI 测试通过，详见 [界面与操作顺序恢复报告](UI-PARITY-preview12.md)。运行时仍无 .NET、WPF、PowerShell 或 WMI 依赖。

preview.11 修正安全弹出的节点选择与错误提示。只对已核对能力和影响范围的独立存储节点请求弹出；磁盘子节点不支持时，可转向同一支 U 盘内已验证的上层存储节点。区分不支持请求、句柄占用、应用/服务阻止、驱动拒绝和权限不足；失败时保留制作成功结果，使用现有提示样式显示“制作完成，U 盘尚未弹出”。857 项断言和窗口测试通过，preview.10 的防闪烁机制保留。详见 [安全弹出修正报告](USB-EJECT-preview11.md)。

preview.10 修正复制时界面频繁闪烁：主窗口绘制排除子控件，进度只使进度区域失效，后台消息合并为约 100 ms 一批，日志批量追加并滚动。完整日志保留，弹窗/结束前立即处理待显示内容。752 项主测试断言通过，另使用生产 UI 进行 5,000 条日志/10,000 次进度更新测试：顶部设置区未重画，纯进度更新不重画日志，末尾日志完整保存。详见 [刷新检查报告](UI-REFRESH-preview10.md)。

preview.9 专项检查格式化、拆分、复制、校验与清理之间的句柄交接。命令执行器在所有退出路径关闭 Job，并核验主进程、已记录的后代进程和 Job 活动计数；复制回调异常改为让 CopyFileEx 先取消收尾，再报告错误；任务返回前统一释放应用持有的文件和目录引用。744 项断言通过（596 项主程序 + 148 项普通文件 FAT32 工具测试），新增真实独占文件、SWM 清理与重复失败无句柄增长检查。具体范围及实机限制见 [句柄生命周期检查报告](HANDLE-REVIEW-preview9.md)。

preview.8 修正 FAT32 工具打开卷时的错误 32：按 Windows 接口要求使用读/写共享打开，再锁卷取得独占访问；只对写入前的临时占用有限等待，持续失败不写入 FAT32。569 项主测试断言通过，新增真实普通文件共享冲突复现、锁定失败及原样保留测试，范围见 [卷打开修正报告](VOLUME-OPEN-preview8.md)。真实 USB 格式化仍待复测。

preview.7 检查并加强新分区建立后的流程：等待卷 GUID / 文件系统识别就绪，NTFS 明确使用无交互参数，按分配单元检查复制空间，核对 SWM 文件头总卷数/编号/集合标识，复制后刷新缓存并逐块比较源文件与目标文件内容，校验显示进度。550 项断言通过（421 项主程序测试 + 129 项普通文件上的 FAT32 工具测试），另验证无效定位失败退出。详见 [后续流程检查报告](POST-PARTITION-preview7.md)。内容校验会增加一次源文件和 U 盘文件读取；它不是介质寿命/坏块检测。

preview.6 修正“清盘核验失败”的状态机：删除旧布局后先单独复核设备身份，再初始化空 MBR；核验新签名及零个有效分区后才建立新分区。等待含有限重试、取消、心跳和具体查询错误，未使用跳过身份检查的兼容分支。334 项自动断言通过，范围和实机限制见 [清盘流程修正报告](DISK-REBUILD-preview6.md)。用户日志已确认目标 ISO 的 WIM 读取和清盘前 DISM 查询通过，完整写盘仍待复测。

preview.5 的任务目录、DISM scratch、持续日志、SWM 清理和新分区就绪改动保留，见 [流程检查报告](FLOW-REVIEW-preview5.md)。此前记录保留：[preview.4](WIM-TEMP-preview4.md)、[preview.3](WIM-TEMP-preview3.md)、[USB 识别](USB-DETECTION-preview2.md) 与 [preview.1 构建](BUILD-REPORT.md)。

## 两个单文件 EXE

| 文件 | 用途 |
| --- | --- |
| `Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe` | 标准权限运行；选择 ISO 路径、读取 USB 整盘名称、容量、盘符和分区数量。不会挂载 ISO、清盘、格式化或复制。编译时不链接制作后端和命令执行器。 |
| `Win11Usb-NativeReview-v0.2.0-preview.14.exe` | 管理员验证候选版；已迁移完整制作后端。清盘前再次检查设备并单独确认，默认取消。仅在已备份、明确允许清空的测试 U 盘上验收。 |

两者均使用原生 x64 PE、系统 MSVCRT 和静态 C++ 运行库，没有 CLR 头或托管程序集。无需额外安装 .NET、VC++ Redistributable、PowerShell 或 WebView。单文件指分发时一个 EXE；制作时仍会在本机创建日志、临时 SWM，并提取独立 FAT32 工具和对应源码/许可。

“去除 .NET”不等于可以在删除任意系统组件的 Windows 中制作。只读窗口依赖 Win32 桌面、GDI+、SetupAPI、存储驱动等基础组件；完整制作还使用 Windows 自带的 `virtdisk.dll`（挂载 ISO）、`wimgapi.dll`（读取映像元数据），大 WIM 拆分使用 `dism.exe`，NTFS 使用 `format.com`。缺少所选流程必需组件时，在清盘前报错。

## 原生制作后端

1. 通过 `SetupDi*` / 存储 IOCTL 枚举 USB **物理磁盘**，不把 PE 的多个分区当成多支 U 盘。排除系统/启动盘、离线、只读和无法确认身份的设备。
2. 直接使用 VirtDisk API 只读挂载手选 ISO，读取 WIMGAPI 的索引 1 元数据。保留原有发行版本冲突检查、文件名辅助说明、LTSC 卷标 `WIN10LTSC` / `WIN11LTSC`。
3. 清盘前核对磁盘编号、接口路径、PnP、序列号、型号、容量、扇区大小和包含磁盘签名的分区布局；确认源 ISO、程序和临时目录都不在目标盘上。缺少完整启动分区映射则禁止写盘。
4. 锁定并卸载目标旧卷，通过 IOCTL 删除整个旧布局。单独复核设备身份及在线/可写属性后初始化空 MBR；连续两次确认设备身份、新 MBR 签名及零个有效分区，才建立一个全容量主分区。未初始化状态不要求分区表查询成功；初始化后不接受 RAW 或旧签名。卷 GUID、物理盘编号和分区范围在格式化前再次核对。格式化成功后分配盘符。
5. FAT32 使用独立 x64 GPL helper，避免精简系统依赖 WOW64；NTFS 使用系统格式化工具。两种文件系统都保持 **4,294,967,295 字节**阈值：超过阈值的 `install.wim` 使用 DISM `/FileSize:4000` 拆分，并逐个检查 SWM 大小和连续编号。分卷数由映像内容决定。
6. `CopyFileExW` 显示当前文件和总体复制进度；复制后先刷新卷缓存，再检查文件大小并逐块比较内容、复核关键 x64 UEFI 启动文件，最后再次刷新缓存及复核设备。校验显示文件和总体进度，支持取消。通过后分别询问临时 SWM 清理、USB 安全弹出、ISO 卸载。

不自动改用 NTFS。NTFS 的“部分 UEFI 固件可能不支持启动”提示保留；当前验收目标为 x64 UEFI，不宣称支持 Legacy BIOS、ARM64 或 WinPE。当前 USB 范围为 8 GiB–2 TiB、512/4096 字节逻辑扇区；全容量包含正常的分区对齐/尾部保留开销。

窗口不启动脚本宿主，没有隐藏 PowerShell 窗口。需要运行的独立命令均使用固定绝对路径和 Windows argv 引号规则，直接 `CreateProcessW`，不经 `cmd.exe`。子进程纳入本次任务的 Job Object；强制结束只针对本程序及其命令。日志持续写入任务目录，窗口关闭不留下后台启动器。

## 构建与测试

从仓库根目录，在命令提示符中执行（构建本身也不需要 PowerShell）：

```bat
native\build.cmd "C:\path\to\llvm-mingw\bin"
native\build.cmd "C:\path\to\llvm-mingw\bin" full
native\test.cmd "C:\path\to\llvm-mingw\bin"
native\test-ui.cmd "C:\path\to\llvm-mingw\bin"
```

已使用官方 [llvm-mingw 20261020](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020)，`llvm-mingw-20261020-msvcrt-x86_64.zip`。下载资产 SHA-256（已核对 GitHub 资产 digest）：`52a3e3f584a74c3cde8ceb0c6960549683178e03b36818cb02b7ed109a68ff67`。编译器/链接器 LLVM 23.1.4，`-static`，GUI 子系统、ASLR、DEP、高熵地址随机化；不加壳、不混淆、不修改 Defender 设置。

标准自动化测试只写入新建的 `work/native-tests-*` 与 `work/native-formatter-tests-*` 文件夹，不访问物理磁盘写入接口。覆盖旧版 39 个版本/卷标用例、文件边界、SWM、流程确认/取消/失败、设备身份快照、真实本机文件复制、Unicode/卷 GUID 路径、任务目录与 NTFS junction 范围、命令参数、持续日志、超时和 Job 取消。四条六阶段文件集成路径使用共用生产函数，但物理格式化、DISM 数据生成和设备弹出由测试替身实现。额外的本地 WIM 生成/元数据回读测试需要管理员备份权限：

```bat
native\out\Win11Usb-NativeTests.exe native\tests\identity-fixtures.tsv work\native-elevated-test-unique --wim-roundtrip
```

该附加测试不操作物理磁盘。普通权限下尝试 WIMCaptureImage 返回 1314，未将此项列为通过。标准测试另含无需捕获权限的实际合成 WIM 只读加载检查，样本说明见 [tests/fixtures](tests/fixtures/README.md)。用户实机日志已确认目标安装 ISO 挂载、WIM 临时目录与索引 1 读取、DISM 查询、Kingston 全容量 FAT32、拆分、复制和内容校验通过。NTFS、其他介质的安全弹出与 UEFI 实际引导等未测范围仍需按 [验收表](ACCEPTANCE.md) 完成，不能用测试替身结果代替实机验收。

## 只读诊断与界面预览

原生预发布的打包使用 Python 3.11 或更新版本，直接读取已检查的制作 EXE，不重新编译。先提交对应源码与文档，再运行：

```bat
python native\package-release.py --exe dist\Win11Usb-NativeReview-v0.2.0-preview.14.exe --expected-exe-sha256 304221B1CDBF32A9A8F9282235EA77EF656B4CE2E1CBD3C2A1D56E81559A1451
```

输出独立版本文件名的 EXE、配套 ZIP、当前 Git 提交的源码 ZIP 和 `SHA256SUMS.txt`。脚本检查原生 PE、版本及指定 EXE 哈希、包内源码和许可；输出已存在则停止，不覆盖旧附件，也不推送或发布。Python 仅用于打包，不是应用运行依赖。

```bat
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --list-disks disks.json
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --eject-report eject-report.txt
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --dependency-report dependencies.json
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --preview
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --preview-blocked
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --preview-running
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --preview-dialog
```

`--preview*` 使用明确标注的示例数据，不是制作结果。弹窗预览点击“预览制作状态”打开，不会执行格式化。系统菜单提供“关于 Win11Usb / 许可证”。JSON 诊断不输出设备原始序列号或 PnP 标识。`--eject-report` 和窗口“导出诊断”中的弹出报告只读取设备属性、匿名父子关系并运行规划，不锁卷、不卸载卷、不提交安全弹出请求；规划通过不等于已经安全弹出。

## 许可与来源

应用自有 C++ 源码采用仓库 MIT 许可证。FAT32 helper 为独立执行的 GPL 程序，保留原始源码归档、修改后的完整 C 源码、GPL 文本和 [修改说明](third_party/NOTICE.txt)，这些文件也嵌入制作版 EXE 并在使用前保存到本次任务目录。原有第三方归档不作修改。

静态运行库的 LLVM/MinGW/winpthreads 许可完整保留于 [RUNTIME-LICENSES.txt](third_party/RUNTIME-LICENSES.txt)，嵌入两个 EXE，可从“关于”查看。主程序 U 盘图标沿用项目原创图标。界面恢复使用 beta.3 的 Material Design Icons 原始路径，通过 GDI+ 绘制；相关 Apache 2.0 文本与上游声明内嵌 EXE，可在“关于”查看。运行时不依赖 WPF、字体图标包或网络资源。
