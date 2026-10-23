# 原生版界面截图

2026-10-23，使用 preview.14 对应的生产 `ui.cpp`、图标与弹窗规格编译 `native/tests/visual_qa.cpp`，通过 Windows Computer Use 捕获实际窗口。该工具在编译阶段排除制作后端，所有设备、ISO 路径、日志和进度均为示例；未执行挂载、格式化、复制或弹出。

- `ready.png`：选择 ISO、整盘与文件系统的就绪主界面。
- `running.png`：复制阶段的六步导航、双进度与日志示例。
- `format-fat32.png`：实际整盘确认弹窗；默认取消，细节区域可滚动。

图片未经绘制替换，不把模拟进度作为实机制作或 UEFI 启动证据。实机结果单独记录于 [原生验收表](../../../native/ACCEPTANCE.md)。
