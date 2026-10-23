# 最终界面复核与紧凑高度：preview.13

日期：2026-10-23。基准为已保存的 GitHub beta.3 发布源码与 `docs/screenshots/ready.jpg`。按用户随后提供的高度要求，主窗口默认尺寸改为 **840×760 DIP**，不再因长内容自动增高；最小尺寸为 720×650 DIP，完整路径通过换行和悬停提示保留。此高度是用户确认的新约束，不按 beta.3 的原始 800 DIP 高度恢复。

## 复核结果

- 主界面的三行设置、原始实心矢量图标、细六阶段进度、日志、主按钮和运行时强制结束按钮保持 beta.3 的布局规则。GDI/GDI+ 与 WPF 的文字/系统控件渲染可能存在像素差异，不宣称逐像素相同。
- 格式化后，拆分、复制与校验自动继续；清理、USB 弹出、ISO 弹出各自询问，最后显示结果。没有新增拆分/复制确认。
- 补齐首次确认中的 NTFS 启动兼容性说明。
- 补齐 beta.3 的停止提示：“任务仍在运行”“继续制作”“默认继续运行，避免误中断。”；主按钮按关闭/强制结束入口分别显示，默认继续运行。格式化仍默认取消。
- 将所有制作弹窗规格集中在 `dialog_specs.hpp/.cpp`。实际 UI 与只读视觉验收使用相同工厂，避免两套文案与默认选项再次偏离。
- 保留内容校验、准确的弹出失败警告、preview.10 防闪烁及 preview.11 安全弹出实现。制作引擎没有因本轮视觉验收重写。

## 实际截图

使用 `native/visual-qa.cmd` 构建的原生只读验收程序；直接编译生产 `ui.cpp`、`dialog.cpp` 和实际弹窗工厂，不链接制作后端。所有设备和进度为明确标注的示例，不访问物理磁盘写入、挂载、格式化、清理或弹出接口。

主界面实测截图为 **826×753 像素**（截图边界不含 Windows 不可见的调整边框），底部按钮完整显示。截图为实际 Windows 窗口捕获，未经生成或重绘。

| 场景 | 截图 |
| --- | --- |
| 就绪主界面 | [01-main.png](test-results/preview13/screenshots/01-main.png) |
| 制作中主界面 | [02-running.png](test-results/preview13/screenshots/02-running.png) |
| 首次确认 / NTFS | [03-start-ntfs.png](test-results/preview13/screenshots/03-start-ntfs.png) |
| FAT32 整盘确认 | [04-format-fat32.png](test-results/preview13/screenshots/04-format-fat32.png) |
| NTFS 整盘确认与兼容性 | [05-format-ntfs.png](test-results/preview13/screenshots/05-format-ntfs.png) |
| 临时文件清理 | [06-cleanup.png](test-results/preview13/screenshots/06-cleanup.png) |
| USB 安全弹出 | [07-eject-usb.png](test-results/preview13/screenshots/07-eject-usb.png) |
| ISO 弹出 | [08-eject-iso.png](test-results/preview13/screenshots/08-eject-iso.png) |

在切换制作完成结果场景时，窗口检查工具返回 `failed to activate captured window`；重新定位后的恢复尝试返回 `GetCursorPos failed: 拒绝访问。 (0x80070005)`，因此停止进一步窗口操作。**制作完成、弹出警告和强制结束三个场景本次未取得人工截图**；完成/警告的实际生产弹窗回归测试及停止提示规格测试通过。没有将缺失截图记为人工验收通过。

## 自动验证

- 76 项新的生产弹窗规格断言通过，覆盖 NTFS、开始/停止默认项、格式化、清理、弹出、成功、取消和失败，未知拆分/复制提示不自动批准。
- 生产 UI 回归通过：5,000 条日志、10,000 次进度、设置区零重绘、完成与警告弹窗、Enter/Esc、720 宽长路径、模拟 144 DPI、完整悬停提示。缩短高度后未出现日志覆盖底部按钮。
- 原 782 项流程断言和 148 项普通文件 FAT32 断言的代码与通过记录保留；本轮未改变其底层实现。真实 USB 制作、实际 DISM 大 WIM 拆分、PnP 移除和 UEFI 引导没有在本轮重做。
- 最终单文件 EXE 的 PE 导入检查及 Defender 文件扫描见 [test-results/preview13](test-results/preview13)。无 CLR 头、仅系统 DLL 导入，应用不依赖 .NET、PowerShell、WPF 或 WMI。完整制作仍需要相应 Windows 存储/映像系统组件。

GitHub beta.3 发布资产未替换。原生候选版应继续完成目标设备上的真实制作与安全弹出验收。
