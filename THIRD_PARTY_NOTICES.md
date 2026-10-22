# 第三方材料与权利说明

根目录 MIT 许可适用于本项目原创代码和文档，不会替换以下独立材料的许可证。

## Ridgecrop fat32format

- 上游：Tom Thornhill / Ridgecrop Consultants Ltd，fat32format 1.07。
- 原始声明：Copyright (c) Tom Thornhill 2007, 2008, 2009；软件受 GPL 约束，原始源码未指定 GPL 版本。本包保留原声明并附 GPL v2 文本。
- [官方项目](http://ridgecrop.co.uk/fat32format.htm) · [原始二进制包](http://ridgecrop.co.uk/download/fat32format.zip) · [原始源码包](http://ridgecrop.co.uk/download/fat32formatsrc.zip)
- 对应源码原包、GPL 文本和完整来源记录保存在 third_party/fat32format。源码文件头旧注释为 1.05，实际帮助输出为 1.07；此处保留上游原包，不修改其内容。
- 工具是独立的原始可执行程序，通过命令行调用，未把格式化 C 代码编译或链接进本项目。EXE 中的压缩包同时带有工具、源码原包和许可，解压后验证固定哈希；运行时不下载。
- 公开源码与发行包均应保留对应源码原包和许可。单文件 EXE 的便利不免除第三方材料的分发要求。

| 材料 | SHA-256 |
| --- | --- |
| fat32format-vendor.zip | 812A33F01C7D73A1E4B89427C01B6BF967DC8D8EF3671200F381B130356B3068 |
| fat32formatsrc-vendor.zip | D1F350F277D14C92D7A0984D9566DD8BAA2592E6B37C63CA657217F251116989 |
| fat32format.exe | D5320A127374AF23139730F0D01AEE8195E5FE15B63C35D48D80930ABBF7F5CB |

## 图标

发行图标已替换为专为本项目生成的原创 USB 图形，未使用此前提供的授权不明图片。生成源图与多尺寸 ICO 随源码提供，项目按 MIT 条款分发这些资产。生成方式、提示词及来源记录见 [图标来源](docs/ICON_PROVENANCE.md)。AI 生成不等同于商标检索或对所有司法辖区著作权状态的保证。

## 界面矢量图标

主界面使用 [Material Design Icons / Pictogrammers](https://github.com/Templarian/MaterialDesign-SVG) 的 13 个开源图标，保留上游 Apache 2.0 条款及声明，不改标为本项目 MIT。

原始 SVG 保存在 assets/ui-icons；固定提交、文件来源及 SHA-256 记录在 third_party/material-design-icons/SOURCES.json。许可原文和上游说明随源码与运行包提供。图形路径转换为 WPF Geometry 并内嵌在单 EXE 中，应用统一着色、缩放；U 盘图标使用显示旋转。运行时不加载字体图标或下载资源。

## Windows 组件

Windows PowerShell、WPF、Storage、DISM、robocopy、Windows 字体及系统 DLL 由 Windows 提供，本发行包不重新分发这些组件。产品与 Microsoft 无隶属或背书关系。
