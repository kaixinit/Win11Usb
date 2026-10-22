# FAT32 打开卷错误 32 修正 — preview.8

日期：2026-10-22。保留现有界面与制作逻辑，专门修正 FAT32 helper 的写入前打开/锁定，不回退或放宽清盘、新分区、SWM、复制及内容校验检查。

## 实机反馈与代码问题

用户 preview.7 日志已依次通过：原磁盘身份复核、空 MBR、新分区及卷 GUID 连续核验。Kingston DataTraveler 3.0，磁盘 3，57.8 GiB；新分区偏移 1,048,576 字节、长度 62,006,493,184 字节、MBR 类型 12（0x0c）。随后 helper 在 `CreateFile` 打开卷时失败，`GetLastError()=32`，没有运行到 FAT32 元数据写入。

旧 helper 沿用供应商代码，以 `dwShareMode=0` 打开卷。Windows 卷直接访问要求设置 `FILE_SHARE_WRITE`，卷锁接口说明要求读/写共享标志。此前文件测试没有保留同时打开的查询句柄，未覆盖这个实际共享冲突。该代码缺陷可以在普通文件上通过真实 Windows 共享机制复现；日志本身不能识别具体哪个进程持有冲突句柄，也不能保证修正共享模式后不存在其他实际文件占用。

## 修正

- 使用 `FILE_SHARE_READ | FILE_SHARE_WRITE` 打开指定卷 GUID；读取物理 extent，核对磁盘编号、偏移、长度，再通过同一句柄 `FSCTL_LOCK_VOLUME` 获取独占访问。锁定后仍保留第二次 extent 与几何/分区类型/隐藏扇区复核，之后才进入 FAT32 写入。
- 只在获取句柄/锁阶段重试临时共享冲突、锁冲突、忙和未就绪；锁卷返回访问拒绝可有限等待（可能存在打开的文件）。打开卷本身的访问拒绝立即停止，不能借重试获取额外权限。实际 extent 不符立即停止且不锁定其他卷。
- 轮询期限 30 秒，200 ms 间隔；状态或错误变化立即输出，同一状态每 5 秒输出。失败先关闭本次句柄，再重试；保留最终系统错误和“opening / checking extent / locking”失败阶段。
- 持续失败不调用 FAT32 写入，不忽略锁失败，不直接强制卸载，不结束其他进程，不重复已经开始的格式化。既有主程序 Job Object 和取消/强制结束仍管理该 helper。
- 主程序核对后已关闭整盘句柄；查询函数使用局部 RAII 句柄；命令仅继承管道句柄。UI 制作中不启动设备扫描。此次未发现需要保留到 helper 的主程序 USB 卷句柄，因此没有添加关闭未知句柄或杀进程逻辑。

## 验证

- 主程序 **421 项**、FAT32 文件测试 **148 项**断言通过，主测试合计 **569 项**。主程序制作/校验行为保留。
- 在普通文件上保留一个真实的共享读取句柄：旧 `share=0` 的读/写打开实际返回 32；生产修改后的打开能成功。随后取得的卷锁仍由测试替身模拟，未据此认定真实 USB 卷锁成功。
- 四条普通文件 FAT32 格式化路径（8/64 GiB × 512/4096 字节扇区）均保留共享读取句柄运行；测试拦截器要求每次生产打开都使用正确共享标志，每次 FAT32 写入之前必须已经锁定且完成两次 extent 核验。原 BPB、备份、FAT、根目录及 >4 GiB 定位检查保留。
- 通过注入时钟及错误，覆盖共享错误后恢复、锁定访问拒绝后恢复、打开权限拒绝立即停止、错误 extent 不锁定/写入、共享与锁定持续冲突在 30 秒轮询期限退出。此处是模拟时钟的状态机覆盖，不是耗时 30 秒的实机占用测试。
- 完整 `format_volume` 负例在锁定持续失败时按原错误 32 退出。另一个独立运行读取预置标记文件，确认原样，随后只删除该测试文件；没有因 helper 返回错误而误写 FAT32。无效定位的非零退出测试仍通过。所有写入仅发生在新建 `work/native-formatter-tests-*` 普通文件中，存储 IOCTL 为替身。
- 完整/只读 x64 单文件编译通过；只读自检、设备与依赖 JSON 退出 0。最终 PE CLR RVA/Size 为 0，仅导入 Windows 系统 DLL，未引入 .NET / PowerShell 依赖。
- 最终两个 EXE 的 Defender 自定义文件扫描退出 0，未检出；未关闭防护或添加排除项。这不是目标机动态格式化检测或硬件验收的证明。

记录见 [test-results/preview8](test-results/preview8/)。

## 目标机复测

本机没有可清空 USB，也不是管理员；未执行真实卷锁或物理格式化。请使用 preview.8 在原来的已备份测试 U 盘复测。日志应先显示 `Confirmed volume opened and locked`，再显示 FAT32 几何、清理 FAT/根目录和文件系统就绪，之后才到 WIM 拆分。

若仍显示等待，需看具体阶段及系统错误。若满 30 秒仍被占用，关闭正在浏览该 U 盘的窗口和其他磁盘工具，再用本程序重试；不跳过锁卷。新分区已建立但 FAT32 可能尚未写入，应以本次日志为准。真实大 WIM 拆分、完整复制、内容校验、安全弹出和 UEFI 启动仍须继续验收。

## 最终文件

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Win11Usb-NativeReadOnly-v0.2.0-preview.8.exe | 1798144 | `fe0b1726cd779d8a88bc298b8449abbe290e1ff271bda68938493fa6a2d98f39` |
| Win11Usb-NativeReview-v0.2.0-preview.8.exe | 2917888 | `6c4d0b91181c66156789a8f012457f23c5cbebcc6b3a24c78ddaa1204684fac5` |

此前命名的 preview.7 和 GitHub beta.3 保持原样，不将未验收的原生候选替换为公开下载。

## 接口依据

- [Microsoft：CreateFile 的 Physical Disks and Volumes 共享要求](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea#physical-disks-and-volumes)
- [Microsoft：FSCTL_LOCK_VOLUME 的共享标志与成功条件](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_lock_volume)
- [Microsoft：FSCTL_DISMOUNT_VOLUME 与锁卷要求](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_dismount_volume)
