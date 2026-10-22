# preview.11：USB 安全弹出与准确提示

日期：2026-10-23。保留现有单窗口布局、六阶段制作规则，以及 preview.10 的子控件裁剪、局部绘制和 100 ms 合并刷新。GitHub beta.3 下载保持不变。

## 问题证据

用户 preview.9 报告显示 Kingston 57.8 GiB 全容量 FAT32 制作、DISM 拆分、1,067 个文件的内容校验、SWM 清理和 ISO 弹出通过，但 USB 弹出返回 `CM 23 / veto 8`。SDK 中分别为 `CR_REMOVE_VETOED` 与 `PNP_VetoIllegalDeviceRequest`，即设备不支持所请求的操作。真正的未关闭句柄是 `PNP_VetoOutstandingOpen`（值 5）。

旧实现直接对所选磁盘的 PnP 子节点调用 CM_Request_Device_Eject，并把所有失败统一显示成“设备仍被使用”。日志没有保存父节点能力，不能单凭它证明该驱动的完整行为；本次修正这种过于固定的节点选择及确定的错误提示缺陷。

## 修正

1. 从已核对的物理磁盘实例沿父链读取设备类型、服务和可移除能力，止于同一物理 USB 存储设备。自动路径仅接受 USBSTOR/UASPStor USB 存储服务；厂商专用、未知服务及无法确认的情况保留给系统安全弹出。
2. 每个候选节点必须具有可移除/可弹出能力，并且其子树仅包含本次选定的一个磁盘及相关存储/卷节点。拒绝其他磁盘、HID/网络等其他功能、集线器、控制器、复合 USB 设备、扩展坞、不可动态移除设备、查询不完整及树循环。不会向任意父节点递归发出弹出请求。
3. 每次尝试前重新核验物理磁盘、分区范围、候选身份和子树范围。随后刷新目标卷缓存、锁定、卸载、解锁并关闭本程序卷句柄，再调用 PnP 移除 API。
4. 原磁盘子节点只在能力确认后才进入候选。如果返回“不支持请求”，仅可尝试同一 USB 范围中另一个已独立核验的候选。暂时未关闭句柄/等待关闭只在当前节点最多尝试 3 次，间隔 500 ms；应用、服务、驱动、权限等其他拒绝不扩大移除范围。
5. 错误提示按实际 CONFIGRET/veto 分类。应用/服务/驱动可显示精简名称；设备实例路径和其中的序列号不写入错误提示。打开卷、刷新缓存、锁定、卸载、解锁的 Win32 失败分别指出具体步骤。
6. USB 弹出失败作为独立警告传回 UI，不改变已经通过的文件复制/内容校验结果。原界面显示“制作完成 · U 盘未弹出”，并使用现有弹窗显示具体原因。仅 CM_Request_Device_Eject 成功时记录“U 盘已安全弹出，可以拔下”。

未关闭其他程序、强制禁用设备、修改驱动/安全设置，也未以跳过身份检查或忽略系统拒绝的方式处理。

## 测试

- `native\test.cmd`：709 项主程序 + 148 项普通文件 FAT32 工具断言，共 857 项通过；另验证 formatter 无效定位失败和锁定超时零写入。
- 共享生产弹出选择/执行函数覆盖：磁盘不可移除时选择同一 USB 存储父节点；磁盘 veto 8 后转向安全父节点；本来支持的磁盘子节点直接成功；临时及持续占用、取消、能力缺失、设备替换、子树变化、循环、查询失败、多个 LUN、其他 USB 功能和集线器/控制器/复合设备等情况。
- 实际文件句柄测试：准备阶段独占打开并刷新一个本机普通文件，返回后请求替身可以立即独占读/写/删除打开，核验阶段交接顺序。此测试不是实际卷锁定或 USB 弹出。
- 在现有磁盘设备上实际读取 CM 实例、类和服务属性，确认原生适配器可用；测试的强制身份校验失败阻止其继续，未请求移除任何主机设备。
- 生产 UI 测试通过：5,000 条日志与 10,000 次进度事件，复制父窗口重画 1 次、日志 WM_PAINT 3 次；纯进度更新不重画日志和设置；日志完整保存。额外向测试窗口注入 USB 弹出失败结果，实际打开原有提示弹窗、核对“不支持”原因并关闭，确认完成状态与未弹出警告同时保留。

测试记录见 [test-results/preview11](test-results/preview11)。上述 PnP 请求和存储控制使用测试替身；本机没有可清空、可弹出的 USB 设备，没有执行真实写盘或真实 USB 安全弹出。源文件 `eject.cpp` / `backend.cpp` 的定向 `-Wall -Wextra` 语法检查通过。

两个最终 EXE 均为静态 x64 原生 PE，CLR RVA/Size 为零，导入表仅含 Windows 系统 DLL；只读自检、设备枚举和依赖报告均返回 0。对最终文件完全一致的副本进行本机 Defender 文件扫描，返回 0 且未检出威胁。这不表示原机器动态检测或设备移除已经通过。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `Win11Usb-NativeReadOnly-v0.2.0-preview.11.exe` | 1,822,208 | `89F3772171C886E375032294248F5EC8E6E0A9F883F45FAE2068E714075C50A4` |
| `Win11Usb-NativeReview-v0.2.0-preview.11.exe` | 2,976,768 | `D545C3903B74D07A1A908BA6E6ECE2ED9B99E5390A5E692DCA43EE6C2E1E1B74` |

## 实机验收

请用新版在已备份、允许清空的原 Kingston 上按原流程验收。重点记录最后的安全弹出结果和完整日志；若仍被拒绝，新的完成弹窗会保留实际原因。只有系统确认安全弹出后才显示可拔下；真实 UEFI 启动和 Defender 动态检测继续单独验收。本版不替换 GitHub beta.3 预发布下载。

参考：[Microsoft CM_Request_Device_Eject](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_request_device_ejectw)、[PNP_VETO_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/cfg/ne-cfg-pnp_veto_type)、[设备能力](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_device_capabilities)、[CM 设备属性查询](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_get_devnode_registry_propertyw)。
