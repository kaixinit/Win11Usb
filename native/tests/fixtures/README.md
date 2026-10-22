# WIMGAPI 元数据读取回归样本

`metadata.wim` 是仅包含 `payload.txt` 的合成 WIM，不包含 Windows 系统文件，不能用来安装 Windows。样本及正文按本项目 MIT 许可证提供。其 Windows 11 Education / 26300.9457 元数据只是测试数据，用于调用系统 WIMGAPI 的真实只读打开、临时目录设置、索引 1 加载和信息读取接口。

样本为 XPRESS 格式，2,058 字节，SHA-256：`e65972cffda846671fc7c28b00f11ff5b82999ad941778fd0e2d088156347c4b`。

生成过程：把 `payload.txt` 放入一个全新目录，使用官方 [wimlib 1.14.5 x64](https://wimlib.net/downloads/index.html) 的 `wimlib-imagex capture`，选项为 `--compress=XPRESS --no-acls --no-check`，映像名称为 `Windows 11 Education`、描述为 `Win11Usb synthetic test fixture`。随后用 `wimlib-imagex info metadata.wim 1 --image-property=键=值` 设置：

| 键 | 值 |
| --- | --- |
| WINDOWS/ARCH | 9 |
| WINDOWS/EDITIONID | Education |
| WINDOWS/INSTALLATIONTYPE | Client |
| WINDOWS/PRODUCTTYPE | WinNT |
| WINDOWS/VERSION/MAJOR | 10 |
| WINDOWS/VERSION/MINOR | 0 |
| WINDOWS/VERSION/BUILD | 26300 |
| WINDOWS/VERSION/SPBUILD | 9457 |

捕获时间戳和随机 WIM 标识会变化，不宣称重新生成后哈希相同。wimlib 仅用于开发时生成这个样本，未嵌入产品或新增运行依赖。下载的官方工具 ZIP SHA-256 为 `2f446d6fa3866582175f1a22a7be198eeee0aec7aba5b4e04ad25c99eae2d265`。

标准测试会将 WIM 复制到本次测试目录、设为只读，使用另一个中文及空格路径作为 WIM scratch；还测试进程内 TEMP 指向不存在目录的情况。它不需要管理员捕获权限，不挂载安装 ISO，不写物理磁盘。额外的 `--wim-roundtrip` 仍是独立的管理员捕获测试。
