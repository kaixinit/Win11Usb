# 发布核对记录

当前版本：0.1.0-beta.3。2026-10-22 已核对用户实机日志并记录全程未报毒反馈，GitHub 仓库与预发布附件已创建。公开状态以 [Releases 页面](https://github.com/kaixinit/Win11Usb/releases/tag/v0.1.0-beta.3) 为准。旧 beta.1 / beta.2 撤回，不随新包分发。

- [x] 保留用户确认的 EXE 字节，不为发布重新编译；产品版本与 SHA-256 见 VALIDATION.md。
- [x] 原生 WPF 启动、标准权限清单；原界面 XAML 和制作 Worker/Engine 保留。
- [x] 11 组回归通过，含原生前端与实际 Worker/Engine 的 17 个集成模拟场景。
- [x] 检查实际预览窗口、默认取消、NTFS 提示、双进度、日志和正常关闭；C# 强制退出独立测试通过。
- [x] 用户报告在原报错电脑正常启动，并完成一次 FAT32 大 WIM 实机制作，全程未报毒；完整日志已核对并脱敏。
- [x] 检查最终候选文件扫描结果、引擎与定义版本；安全限制和旧版事件已说明。
- [x] MIT、原创应用图标、Apache-2.0 图标声明、第三方 GPL 及对应原始源码保留。
- [x] 运行包、源码包及内外校验清单通过检查；核对文件完整性、文档链接与脱敏，源码包解压后重新构建并通过全部 11 组测试。
- [ ] 从 USB 实际引导并进入安装程序；NTFS 固件支持另行记录。
- [ ] 当前版本的 NTFS、小 WIM、大于 32 GB FAT32、多分区 PE 盘实机矩阵。
- [ ] Windows 10、不同 DPI 和工作区、完整键盘/辅助技术检查。
- [ ] UAC 取消、真实驱动阻塞及热拔插验证。
- [x] 维护者 kaixinit；公开仓库已创建，Issues 和私密安全反馈已启用。
- [x] GitHub Actions Windows 构建、11 组回归及打包通过，记录见 VALIDATION.md。
- [x] 源码与三个预发布附件已上传并完成下载哈希核对；不上传工作目录、原始日志或旧 EXE。

建议仓库描述：Offline Windows installation USB creator with whole-disk checks, WIM splitting and live progress.

建议 Topics：windows、usb、windows-installation、wpf、powershell、fat32、ntfs、portable。
