# 原生验证构建报告

日期：2026-10-22。版本：0.2.0-preview.1。状态：**原生代码已迁移，实盘验收未完成**。

## 已通过

- 164 项自动断言：旧版 39 组版本/卷标数据、4 GiB 边界、SWM、六阶段 FAT32/NTFS 流程、确认/取消/失败、设备快照、真实本机 Unicode 文件复制、文件大小校验、参数转义、超时、Job 取消及后代进程结束。
- 原生只读窗口启动、设备刷新、FAT32/NTFS 切换、弹窗预览、回车默认取消、关闭后进程退出。
- 三个最终二进制都是 x64 PE，CLR 数据目录的 RVA/Size 均为 0；未导入 mscoree/coreclr 或独立 C++ 运行库 DLL。只读窗口实际进程模块检查也未发现 CLR / System.Management.Automation。
- 原生只读诊断退出码 0；当前主机没有 USB 盘，系统/启动分区映射成功。本机 virtdisk.dll / wimgapi.dll 可加载，dism.exe / format.com 存在。
- 最终只读 EXE、制作候选 EXE 和 FAT32 helper 的 Defender 自定义文件扫描退出码 **0，未检出威胁**。实时保护开启，签名版本 `1.461.145.0`。这是本机文件扫描结果，不代表其他机器或完整制作过程的动态检测结果。
- beta.3 EXE SHA-256 仍为 `35bb091183070f495429611641c596289f692e055dcae9d83b293b66ae7aadeb`，没有替换 GitHub 下载。

## 尚未通过 / 未执行

- WIMCaptureImage 本地文件回读扩展测试在普通权限下尝试失败，错误 1314。未计入通过数量；测试工具保留管理员 `--wim-roundtrip` 模式。
- 当前没有可清空的 USB 盘、管理员写盘权限或 Windows 安装 ISO。未执行真实 ISO 挂载、WIM/SWM/ESD 元数据读取、DISM 大 WIM 拆分、FAT32/NTFS 写盘、整盘复制、安全弹出和 UEFI 引导。
- 尚待目标精简 Windows 的只读识别反馈，以及原报毒电脑上的完整原生制作动态检测。

完整验收项目见 [ACCEPTANCE.md](ACCEPTANCE.md)。在这些项目通过前，原生制作候选不能视为可发布版本。

## 最终二进制

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Win11Usb-NativeReadOnly-v0.2.0-preview.1.exe | 1747968 | `63c86203b62ecf12b9ec969900b4d9c8f90a2e6a7011905c6beaf9dd33562699` |
| Win11Usb-NativeReview-v0.2.0-preview.1.exe | 2733056 | `a556f38d26e605480ffc634257f45f15ef893d53f3554dcb761441e30a56a9ae` |
| fat32format-x64.exe | 201728 | `bf63ee484c78d157e439b9fab126f64de2d2c703a015ef5d31cc029aa1da26da` |

FAT32 helper 嵌入制作 EXE，用户不需要单独下载。编译器固定为 llvm-mingw 20261020 / LLVM 23.1.4（msvcrt x86_64）；原生源码、构建说明、第三方许可和相应源码均已保留。构建时间戳未去除，不宣称逐字节可复现。

机器可读记录位于 [test-results](test-results/)；这里不包含原始设备序列号、PnP 标识或用户安装介质。