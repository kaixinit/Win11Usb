# 参与贡献

先阅读 [架构说明](docs/ARCHITECTURE.md) 和 [验证记录](docs/VALIDATION.md)。使用 Windows PowerShell 5.1 构建和运行 tests/Run-Tests.ps1。

磁盘相关修改需要覆盖：取消不修改磁盘、设备编号复用、源 ISO 或运行文件位于目标盘、布局变化、格式化失败、复制失败与校验失败。测试必须模拟存储和弹出边界，不应默认操作开发者的真实磁盘。

提交中请说明问题、修复后的行为和验证方式。不要提交个人会话日志、Windows ISO、密码、设备序列号或未经授权的素材。PowerShell 文件使用 UTF-8 BOM，保证 Windows PowerShell 5.1 正确读取中文。

项目原创源码按 MIT 许可接收贡献。third_party 中的内容继续遵守各自许可证；不要把第三方二进制或对应源码改标为 MIT。
