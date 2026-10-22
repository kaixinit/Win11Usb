# 原生迁移验收记录

日期：2026-10-22。候选版本：0.2.0-preview.2。beta.3 发布文件保持不变。

## 已完成的验证

- x64 原生只读窗口可以启动、切换 FAT32/NTFS、刷新和关闭；空设备列表时禁用写入操作。
- 当前主机原生设备枚举返回零个 USB 磁盘，启动分区映射完整；没有把内置磁盘列为 USB 候选。
- 完整制作后端与只读版均可用 llvm-mingw 编译。只读版在编译时排除 ISO 挂载、格式化、复制和子进程执行实现。
- 自动化流程测试包含 FAT32/NTFS 成功路径、全部六阶段、格式化/拆分/复制确认取消、准备/复核/格式化/拆分/复制/校验错误、清理/弹出失败提示，以及开始/确认后取消的停止处理。
- 实际本机文件测试覆盖原生枚举、中文和空格路径、CopyFileEx 进度、不覆盖未知目标文件、文件大小不匹配和取消。
- 实际子进程测试覆盖参数内的反斜杠、引号、中文、空格、`$()`/反引号/管道符作为字面参数，以及命令超时和 Job 取消。
- 原有 39 组映像版本/发行版本/卷标用例全部通过；包含 2019/2021 LTSC、Windows 10/11 共享构建号、24H2/25H2/26H2、文件名冲突和 Server 优先识别。
- 用户提供 preview.2 实机截图并反馈“可以下拉选择了”：普通权限下列出 Samsung PSSD T7 Touch（磁盘 2、931.5 GiB、F:、1 个分区）和 Kingston DataTraveler 3.0（磁盘 3、57.8 GiB、G:、1 个分区）；日志为 4 个磁盘接口、2 个 USB、2 个非 USB、0 个查询失败。两个内部 SSD 未列入 USB 下拉框。此项证明只读显示和选择恢复，不代表已经完成写盘验收，也未独立核验目标系统删减了哪些组件。

preview.2 的 186 项自动断言、PE 依赖检查和 Defender 文件扫描结果见 [USB-DETECTION-preview2.md](USB-DETECTION-preview2.md)；[构建报告](BUILD-REPORT.md) 保留 preview.1 历史记录。**自动化通过不表示真实 USB 写盘或所有精简系统兼容性已通过。**

## 未完成 / 需要真机完成

当前环境没有可清空的 USB 设备，测试进程也没有管理员权限。额外 WIMCaptureImage 文件测试尝试返回 1314（缺少备份权限），并未把它记为通过。以下均不能根据模拟后端结果宣称通过：

| 测试 | 验收标准 | 状态 |
| --- | --- | --- |
| 精简 Windows 只读启动与设备选择 | 只复制只读 EXE 可打开；能识别真实 USB 名称、容量、盘符和分区数 | 用户反馈 preview.2 可下拉选择，截图显示两支 USB；具体删减组件未独立核验 |
| PE 多分区 U 盘 / 无盘符 | 一个物理磁盘条目，分区计数正确；最终只剩一个主分区 | 待测 |
| 大容量 FAT32 | 64/128 GB 等 USB 整盘格式化为 FAT32，未截成 32 GB | 待测 |
| NTFS | 完整提示兼容性；成功格式化；超过阈值 WIM 仍拆分 | 待测 |
| 自动 ISO 挂载 | 手动选择 ISO 后正常挂载、读取 index 1、识别系统；既有挂载可复用 | 待测 |
| 安装文件范围 | WIM 小于/超过阈值、ESD、已有连续 SWM 均与预检规则一致 | 待测 |
| 大 WIM 拆分 | DISM 成功退出；每卷大于 0、不超过 4,294,967,295 字节、编号连续 | 待测 |
| 真实复制 / 缓存 | 完整文件大小校验通过、启动文件存在、缓存刷新成功 | 待测 |
| 安全弹出 / 清理 | 各项有明确选择；失败保留制作成功状态并提示；只清理本次 SWM | 待测 |
| 关闭 / 强制结束 | ISO 准备、拆分和复制期间确认结束后窗口退出，无本应用子进程遗留 | 自动命令取消已通过，真实制作待测 |
| Defender 动态检测 | 原报毒机器启动与完整制作无检测；保持实时保护开启 | 本机文件扫描另附，目标机待测 |
| UEFI 启动 | FAT32 制作盘在真实固件启动到 Windows 安装界面 | 待测 |
| DPI / 分辨率 | 目标系统常用缩放比例下，选择器、完整名称、弹窗按钮、日志均可操作 | 96 DPI 已检查，其他待测 |

请先用只读版确认精简系统启动和设备识别。通过后，在一支已备份、明确允许清空的测试 U 盘上执行原生制作候选版；每次按弹窗核对物理磁盘名称、编号和容量，保留任务目录中的 `session.log`、DISM/format 日志。遇到 Defender 检测应保留隔离状态，不添加排除项。验证完成前不替换 GitHub beta.3 下载。

## 实现参考

- [Microsoft：AttachVirtualDisk](https://learn.microsoft.com/en-us/windows/win32/api/virtdisk/nf-virtdisk-attachvirtualdisk)
- [Microsoft：OpenVirtualDisk 参数和只读深度](https://learn.microsoft.com/en-us/windows/win32/api/virtdisk/ns-virtdisk-open_virtual_disk_parameters)
- [Microsoft：分区布局 IOCTL](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_disk_set_drive_layout_ex)
- [Microsoft：WIMGetImageInformation](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/wim/dd834949(v=msdn.10)?view=windows-11)
- [Microsoft：format 命令的卷名参数](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/format)
