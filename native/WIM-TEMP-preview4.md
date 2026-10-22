# preview.4：首次创建 WIM 临时目录的顺序修复

日期：2026-10-22。

用户的 preview.3 日志显示 ISO 已挂载到 H:，随后 `wim-temp` 路径校验返回错误 2，任务仍在第 1 步，尚未清盘。

## 原因与修改

制作调用方先执行 `AssertOutside(wim-temp)`，再调用负责创建目录的 WIM 读取函数。`AssertOutside` 内部使用文件句柄解析实际路径，要求路径已存在，首次运行就提前失败。preview.3 测试覆盖了低层 WIM 读取，却未覆盖调用方的这段顺序，这是此次回归的缺口。

新增 `ReadTaskWimIdentity`，制作后端和集成测试使用同一入口：先验证已存在的本次任务父目录不在目标盘上，创建固定子目录 `wim-temp`，然后核对新目录的实际磁盘，最后设置 WIM 临时目录并读取索引 1。没有放宽路径或目标盘保护。

其余 `AssertOutside` 调用点也已核对：ISO 和程序路径必须存在，任务根目录由 `NewWork` 创建后再校验。没有把不存在路径解析成父目录并跳过实际子目录核验。

## 验证结果

- **207 项自动断言通过**。新增 10 项覆盖制作实际使用的入口，包括全新任务没有 `wim-temp`、真实创建/实际卷查询/WIMGAPI 读取成功、目标盘上任务目录在创建前被拒绝、空/不存在父目录、子路径已被文件占用，以及重用本任务目录。
- 既有实际只读 WIM、无效 TEMP、中文/空格路径、句柄释放、错误与取消流程测试仍通过。未写入物理磁盘。
- 最终两个 EXE 编译通过，x64 PE 的 CLR 目录 RVA/Size 为 0，静态导入仅 Windows 系统 DLL；只读资源自检和实际设备枚举退出 0。
- Defender 对最终两个 EXE 的本机自定义文件扫描退出 0，报告 `found no threats`，未禁用防护或设置排除。此结果不是完整制作动态检测。
- 当前没有可清空的真实 USB 或目标安装 ISO，真实挂载后的完整制作、FAT32/NTFS 写盘和 UEFI 启动仍需目标电脑验收。没有把合成 WIM 测试计为整个第 1 步或全部六步实机通过。

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Win11Usb-NativeReadOnly-v0.2.0-preview.4.exe | 1795072 | `9b46c671b66529af8c9e127fab280c00e142a5a5a9ee6b6fbaa23ad639cfd225` |
| Win11Usb-NativeReview-v0.2.0-preview.4.exe | 2787328 | `271f7540e44e95ee4b7d9a46cc0c7835e75138960f42a7d54be5a5d4791df88b` |

机器可读记录见 [test-results/preview4](test-results/preview4/)。保留现有界面、制作顺序和 4 GiB 拆分规则；preview.1–3 和 GitHub beta.3 的原有二进制未覆盖。
