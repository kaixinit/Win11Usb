# 原生清盘后重建流程修正 — preview.6

日期：2026-10-22。保留现有界面、六阶段制作顺序、全容量 FAT32、NTFS 同阈值拆分、复制/大小校验及清理/弹出交互。此次仅修正磁盘布局转换与诊断，不替换 GitHub beta.3。

## 用户反馈与证据范围

用户提供的 preview.5 日志：ISO 成功挂载，WIM scratch 可写且有约 129 GiB 可用空间，索引 1 读取成功，DISM 只读查询成功，识别 Windows 11 Education 26H2 / 10.0.26300.9457；Kingston DataTraveler 3.0，磁盘 3，57.8 GiB。随后“删除磁盘 3 的全部旧分区…”之后报“清盘核验失败，未建立新分区”。

这确认此前的 WIM 目录修复已通过该环境的实际读取，失败位置转移到删除布局之后。旧版错误将身份不完整和分区非空合并，未保留当时的查询错误/分区状态，**无法从已有日志确认具体驱动返回值或认定旧分区仍存在**。旧分区可能已删除。

## 代码中确认的问题与修正

1. **未初始化的中间状态依赖完整布局查询。** 原流程删除布局后立即 `ReadDisk`，而 `SameIdentity` 又要求布局完整；只有“身份完整且分区非空”才重试。布局/描述符等查询暂时失败时，直接停止。
2. **调整重建顺序。** 旧布局仍在时保留严格身份、布局及启动盘排除检查；删除布局后，单独读取型号/序列号、编号、容量/扇区和在线/可写属性。连续两次一致后，使用原先保留的整盘句柄调用 `IOCTL_DISK_CREATE_DISK` 初始化空 MBR。然后连续两次核验原设备身份、新 MBR 签名与零个有效分区，再允许 `IOCTL_DISK_SET_DRIVE_LAYOUT_EX` 建立一个分区。初始化后不接受 RAW、旧签名或实际非空布局。
3. **不把布局缺失误作身份变化。** 增加独立的 `descriptorKnown` 状态。没有读到描述符时，设备树的临时显示名不用于断言型号变化；仍等待完整身份。任何已知的编号、接口、PnP、容量/扇区、型号/序列号冲突，以及只读/离线状态，均立即停止。原有 `SameIdentity` 与清盘前布局核验未放宽。
4. **等待有限且有反馈。** 身份与空 MBR 两阶段各以 30 秒为轮询期限，200 ms 间隔，连续两次就绪才通过。状态变化立即记日志，相同状态每 5 秒心跳；支持取消。查询/刷新错误保留具体系统错误，初始化失败不反复写入。这个期限约束程序轮询，无法保证同步驱动 IOCTL 本身永不阻塞；仍可使用既有强制结束。
5. **正确区分分区表槽位与有效分区。** 共用布局解析器按类型跳过 MBR 类型 0 / GPT 空类型的未使用槽位，不再仅凭残留长度计数。保留原始槽位数用于诊断，并拒绝截断、未知布局类型、真实分区负数/越界范围；不把实际分区忽略成空盘。
6. **诊断可落地。** `session.log` 通过既有日志通道保存阶段、编号、查询已知状态、实际分区偏移/长度、MBR 签名和查询/刷新错误。不输出原始序列号或 PnP。JSON 设备诊断补充 `descriptor_known` 和 `layout_entries`。生成新磁盘标识前移至删除布局之前。

该顺序基于 Windows 接口语义：删除操作移除磁盘分区信息；创建操作初始化磁盘与分区表；刷新操作使缓存布局失效并重新枚举。没有将删除操作成功等同于最终可格式化，也没有用忽略核验或 DiskPart 兜底继续写盘。硬件兼容性的具体判断仍是待实测推断。

## 已执行的验证

- **334 项断言通过**，较 preview.5 增加 67 项。新增测试直接调用生产 `InitializeEmptyMbr` 与 `ParseDriveLayout`，磁盘 IOCTL 通过测试替身返回数据，不操作物理盘。
- 覆盖删除后布局不可查询但身份可确认、描述符暂缺且回退型号变化、布局暂缺/抛错、旧签名、旧实际分区、未使用槽位残留长度、连续两次确认、30 秒超时、5 秒心跳、刷新失败、初始化失败及两阶段取消。
- 两阶段分别注入 9 类已知设备/属性变化，验证即使伴随其他查询不完整也立即退出，后续不建立分区。
- 原有 39 组映像识别、目录固定、真实 WIM 读取、Unicode/卷 GUID 文件复制、四条六阶段文件集成路径、命令取消/超时与后代进程、真实 NTFS junction 清理范围测试仍通过。物理格式化、DISM 输出数据、弹出仍用替身，未记作硬件验收。
- 完整与只读 EXE 编译通过；`-Wall -Wextra` 语法检查成功，仍有两条既有提示：未使用的 `IsEfi`、`MONITORINFO` 聚合初始化提示。
- 最终只读 EXE 的 `--self-check`、`--list-disks`、`--dependency-report` 均退出 0。本机无可清空 USB，只有一块非 USB 的虚拟磁盘；枚举未误列为候选。
- 两个最终 EXE 的 CLR RVA/Size 为 0，仅导入 Windows 系统 DLL；仍为原生 x64 单文件分发，无 .NET/PowerShell 运行依赖。
- Defender 对两个最终 EXE 的副本执行自定义文件扫描，退出 0，`found no threats`。没有关闭防护或添加排除项。未据此宣称目标机动态制作检测通过。

机器记录：[test-results/preview6](test-results/preview6/)。合成状态日志也保存在该目录，明确不是实际写盘结果。

## 仍需目标机确认

本机没有管理员写盘测试条件和可清空 USB。尚未实际执行此次 IOCTL 删除、空 MBR 初始化、新分区创建、FAT32/NTFS 格式化及完整 ISO 制作。需要在原报错机器的已备份测试 U 盘上复测 preview.6，确认日志依次显示“清盘后设备身份复核通过”“空 MBR 核验通过”“新分区已核验”，再检查拆分、复制、校验和 UEFI 启动。

若仍停止，应保留本次 `session.log`，以新增的阶段、实际状态及系统错误定位原因。不要只凭泛化错误再次修改放宽条件。

## 最终文件

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Win11Usb-NativeReadOnly-v0.2.0-preview.6.exe | 1798144 | `1a3d5b6ddb77bfe02c17dc3152837c244ac865cd5a741fc0f26fcf85cb9a8fef` |
| Win11Usb-NativeReview-v0.2.0-preview.6.exe | 2847232 | `eae0c6cf504f209e11b521a336eb15048b07f132f0f924baa913a29ebdf7cc18` |

此前命名的验证版和 beta.3 文件保留，没有覆盖为此版本。

## 接口依据

- [Microsoft：IOCTL_DISK_DELETE_DRIVE_LAYOUT](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntdddisk/ni-ntdddisk-ioctl_disk_delete_drive_layout)
- [Microsoft：IOCTL_DISK_CREATE_DISK](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_disk_create_disk)
- [Microsoft：IOCTL_DISK_GET_DRIVE_LAYOUT_EX（含未使用槽位）](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_disk_get_drive_layout_ex)
- [Microsoft：IOCTL_DISK_UPDATE_PROPERTIES](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_disk_update_properties)
