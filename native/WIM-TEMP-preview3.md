# preview.3：修复 WIM 索引读取错误 1632

日期：2026-10-22。

后续实机反馈：本页的低层 WIM 读取修复通过测试，但制作调用方在创建 `wim-temp` 前先校验该路径，导致错误 2。这段调用顺序已在 [preview.4](WIM-TEMP-preview4.md) 修复，并加入共用生产入口的集成测试。本页保留 preview.3 的历史记录。

用户在 preview.2 中已成功只读挂载 ISO 到 H:，随后读取安装映像索引 1 返回 1632。任务停在第 1 步，没有开始清盘。

## 原因和修复

原生移植中，`WIMCreateFile` 后直接调用 `WIMLoadImage`，遗漏了必需的 `WIMSetTemporaryPath`。微软的 [WIMLoadImage 文档](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/wim/dd834941(v=msdn.10)?view=windows-11) 明确要求加载前设置临时目录，以便提取并处理映像元数据。此处 1632 不能单凭提示文字认定用户 C 盘已满。

已改为在本次独立任务目录下创建 `wim-temp`，确认路径在目标 USB 外、目录可写且至少预留 64 MiB 后，通过 `WIMSetTemporaryPath` 绑定到本次 WIM 句柄，再加载索引。64 MiB 是开始读取前的最低余量，并不保证能容纳所有映像的元数据；系统接口若仍失败，会保留具体路径与错误码并在清盘前停止。日志显示实际临时目录和可用空间，不依赖用户的 TEMP/TMP 设置，不修改全局环境变量或目录权限。

WIM 文件和索引句柄使用配对的 `WIMCloseHandle` 自动释放；写入检查文件关闭即删除。界面保留原有布局、制作顺序和确认逻辑，并避免把同一条“已停止”错误重复写两次。大容量 FAT32、NTFS 和 4 GiB 拆分规则不变。

## 验证

- 使用仅含自有测试文本的 [2 KiB 合成 WIM](tests/fixtures/README.md)，在本机旧版读取函数上复现了同样的索引 1 / 1632 错误。
- 相同样本使用修复后的真实 WIMGAPI 成功读取到测试元数据 Windows 11 Education、10.0.26300.9457、x64、WIN11_26H2。
- 197 项自动断言通过，其中新增 11 项覆盖真实 WIM、只读源文件、中文/空格 scratch、缺失 scratch、scratch 路径为文件、损坏 WIM、句柄释放、写入检查文件清理，以及进程内 TEMP 指向不存在目录时仍正常读取。测试未写物理磁盘。
- 额外的管理员 WIM 捕获测试未执行；真实安装 ISO 的大 WIM、FAT32/NTFS 格式化、完整复制及 UEFI 启动仍待目标电脑验证。
- 两个最终 EXE 为 x64 PE，CLR 数据目录 RVA/Size 为 0；静态导入仅 Windows 系统 DLL。只读 EXE 的资源自检、依赖报告和实际设备枚举均退出 0。本机只有虚拟磁盘，未把它识别为 USB。
- Defender 对最终两个 EXE 的本机自定义文件扫描退出 0、`found no threats`。没有禁用防护或设置排除；此项不代表其他电脑或完整制作动态检测通过。

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Win11Usb-NativeReadOnly-v0.2.0-preview.3.exe | 1795072 | `34637aadaf77a58063ab904f18e8adc561d7c66d7826fe73e0b922e8ec350387` |
| Win11Usb-NativeReview-v0.2.0-preview.3.exe | 2785280 | `0a4473c0a2162d72c3ddce4f7bee5652e80d9409f2df9cdb5fe9031451ffc2a2` |

本次机器可读记录见 [test-results/preview3](test-results/preview3/)。preview.1、preview.2 和 GitHub beta.3 的原有二进制未被覆盖。
