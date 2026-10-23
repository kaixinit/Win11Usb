# 参与贡献

当前开发主路径为 `native/` 中的 C++ / Win32 原生版。先阅读 [架构说明](docs/ARCHITECTURE.md)、[原生构建说明](native/README.md) 和 [原生验收记录](native/ACCEPTANCE.md)。原生版应用运行时不需要 .NET、PowerShell 或 VC++ Redistributable；构建工具与操作系统自带的制作组件是另一组依赖，不能据此宣称支持任意精简 Windows。

## 构建和测试原生版

使用 x64 Windows 和官方 [llvm-mingw 20261020](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020) 的 `llvm-mingw-20261020-msvcrt-x86_64.zip`。资产 SHA-256 为 `52a3e3f584a74c3cde8ceb0c6960549683178e03b36818cb02b7ed109a68ff67`。在仓库根目录的命令提示符中执行：

```bat
if not exist work mkdir work
native\build.cmd "C:\path\to\llvm-mingw\bin"
native\build.cmd "C:\path\to\llvm-mingw\bin" full
native\test.cmd "C:\path\to\llvm-mingw\bin"
native\test-ui.cmd "C:\path\to\llvm-mingw\bin"
python native\verify-pe.py dist\Win11Usb-NativeReadOnly-v0.2.0-preview.14.exe dist\Win11Usb-NativeReview-v0.2.0-preview.14.exe
```

Python 3 只用于只读 PE 检查，不是应用运行依赖。标准流程测试只写入本次新建的 `work/native-tests-*` 和 `work/native-formatter-tests-*` 普通文件，存储写入及实际弹出使用替身；真实设备属性查询为只读。UI 测试直接使用生产界面代码和自己的测试窗口，不链接制作后端，检查批量日志、进度刷新、弹窗和默认按钮。

测试输出的断言数会随本机设备和可用权限变化，以退出状态、各项 `PASS` / `NOT RUN` 和保存的日志为准。额外的 `--wim-roundtrip` 测试需要管理员备份权限，不属于默认 CI 验收。普通文件测试不能证明真实 U 盘格式化、安全弹出或 UEFI 启动；硬件验收结果应注明版本、设备、系统和未测范围，不提交原始身份信息。

GitHub Actions 在 Windows 2022 上校验固定工具链哈希，分别构建只读版和完整版，运行上述非破坏性测试及 PE 检查，并保留候选 EXE 和测试日志。CI 不自动发布 Release，不操作真实 USB；UI 测试失败会使任务失败，不自动跳过桌面环境问题。

## 修改与提交

磁盘相关修改需要覆盖：取消不修改磁盘、设备编号复用、源 ISO 或运行文件位于目标盘、布局变化、格式化失败、复制失败与校验失败。测试必须模拟存储和弹出边界，不应默认操作开发者的真实磁盘。

提交中请说明问题、修复后的行为和验证方式。不要提交个人会话日志、Windows ISO、密码、设备序列号或未经授权的素材。原生 C++ 与文档使用 UTF-8。设备诊断先去除原始序列号、PnP 标识、本机用户名和任务目录，再作为公开验收材料。

## 历史 beta.3 维护

`src/`、`build/` 和 `tests/` 保留 beta.3 的 C# / WPF / PowerShell 实现及回归测试，供旧版维护和行为对照。其构建方式仍为 Windows PowerShell 5.1 执行 `build/Build-Win11Usb.ps1` 和 `tests/Run-Tests.ps1`，验证边界见 [历史验证记录](docs/VALIDATION.md)。修改历史 PowerShell 文件时保留 UTF-8 BOM，保证 Windows PowerShell 5.1 正确读取中文。原生预发布使用独立标签和版本文件名，不覆盖 beta.3 发布资产。

项目原创源码按 MIT 许可接收贡献。third_party 中的内容继续遵守各自许可证；不要把第三方二进制或对应源码改标为 MIT。
