# preview.14：修正 USB 设备类别识别

日期：2026-10-23。保留 preview.13 的 840×760 DIP 默认窗口、beta.3 图标和弹窗顺序、内容校验、句柄交接、防闪烁，以及现有格式化、拆分和复制实现。GitHub beta.3 发布文件不变。

## 本次日志说明了什么

用户新提供的日志没有记录应用版本。Kingston DataTraveler 3.0（57.8 GiB）完成全容量 FAT32、2 个 SWM、1,067 个文件复制及逐文件内容校验、临时 SWM 清理和 ISO 弹出；没有“已停止”记录。

仅 USB 安全弹出失败，具体为“未能确认所选 U 盘对应的独立存储设备”。此错误由 `PlanUsbEject` 在设备匹配阶段返回，尚未进行弹出准备或提交 PnP 移除请求。因此，这次日志不能归因于未关闭句柄、权限或驱动 veto。ISO 卸载与 USB 安全弹出是两条独立操作。

去除用户名、任务目录和设备实例信息后的检查结果见 [用户日志核对](test-results/preview14/user-log-summary.json)。日志没有证明真实 UEFI 启动、新版设备移除或 Defender 动态验收。

## 根因与修改

原 `ReadEjectNode` 将 `{4d36e97d-e325-11ce-bfc1-08002be10318}` 当成 USB 类；该值实际是 `GUID_DEVCLASS_SYSTEM`。真正的 `GUID_DEVCLASS_USB` 为 `{36fc9e60-c465-11cf-8056-444553540000}`。普通 USBSTOR 父节点因此被归为未知类别，规划在物理 USB 存储边界前停止。这是应用代码中的确定缺陷；原用户日志未保存设备类别，不能据此保证该设备不存在其他弹出限制。

现在统一通过 Windows SDK 常量分类，不再手写这组类别值。System 和未知类继续拒绝；USBSTOR/UASPStor 服务、独立物理 USB 边界、可移除能力、唯一目标磁盘和安全子树检查保留。不向集线器、控制器、复合设备或多磁盘父节点扩大移除范围。

原有成功/失败规则保留：仅系统接受移除请求时显示可以拔下；失败保留制作成功结果并提示具体原因。

## 只读诊断

现有系统菜单“导出设备诊断…”增加匿名设备类别、服务、能力和父链规划结果。也可运行只读版：

```bat
Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe --eject-report eject-report.txt
```

报告只读取 CM 属性和关系，再调用生产共用规划函数。不会锁卷、卸载卷、写盘或请求移除；只读版从编译阶段排除两个实际弹出入口，且导入表不包含 `CM_Request_Device_EjectW` 和 `CreateProcessW`。报告隐藏原始 PnP、设备序列号和文件路径，规划通过不等于设备已经弹出。

## 验证结果与范围

- 814 项主程序断言 + 148 项普通文件 FAT32 断言，共 962 项通过；另外验证无效定位和锁定超时不写入 FAT32。
- 新增 SDK 原始 GUID → 生产分类 → 设备树规划的回归，覆盖 USBSTOR、UASP/SCSIAdapter、System 拒绝、无效 GUID，以及已有多 LUN、复合设备、身份变化和句柄交接检查。旧测试直接注入类别枚举，未覆盖错误映射；此缺口已补上。
- 生产 UI 模拟 5,000 条日志、10,000 次进度事件通过；设置区重画 0 次，复制期日志重画 3 次，纯进度阶段日志重画 0 次，完整日志保存，完成和弹出失败提示正常。弹窗键盘、长路径和缩放回归通过。
- 两个最终 EXE 均为 x64 GUI 原生 PE，无 CLR 头；静态应用运行库，系统 DLL 导入检查通过。只读自检、依赖报告、弹出报告均返回 0。
- Defender 1.461.155.0、实时保护开启，对两个最终 EXE 自定义文件扫描均返回 0、未检出。文件扫描不代表目标机器动态检测已通过。
- 本机仅有 1 个内置磁盘，没有 USB 存储设备。真实 USB 类属性及物理祖先规划明确记为 NOT RUN；未向本机设备发出任何移除请求。真实目标 U 盘弹出仍待复测。

原始测试输出和报告见 [test-results/preview14](test-results/preview14)。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe` | 1,963,520 | `8C04C9C7381F8387F7F54AD9A8F70E9E1BBC20E04323E30211104D1BD10AB9EA` |
| `Win11Usb-NativeReview-v0.2.0-preview.14.exe` | 3,090,944 | `304221B1CDBF32A9A8F9282235EA77EF656B4CE2E1CBD3C2A1D56E81559A1451` |

本次已完成的 U 盘无需为消除弹出警告重新制作，可以使用 Windows“安全删除硬件”。新版自动弹出在后续正常制作时验收；若仍失败，先导出只读诊断再核对，不需要为取得报告重新格式化。

参考：[Microsoft 系统保留的设备安装类](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/system-defined-device-setup-classes-reserved-for-system-use)、[可用设备安装类](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/system-defined-device-setup-classes-available-to-vendors)、[CM_Request_Device_EjectW](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_request_device_ejectw)。
