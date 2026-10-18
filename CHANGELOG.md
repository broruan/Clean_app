# Changelog / 更新记录

## [1.1.2] - 2026-10-18

### English

- Prepare public distribution with an English README, a complete Chinese README, contribution and security guides, and source / validation documentation.
- Refresh the original SVG application icon and use the same asset in the application, desktop entry, and documentation.
- Add native build and test jobs targeting Ubuntu 22.04, 24.04, and 26.04 on `amd64` and `arm64`.
- Generate release-specific `.deb` names and shared-library dependencies, with six-package release aggregation and `SHA256SUMS`.
- Keep the existing Chinese interface and its storage overview, package inventory, uninstall preview, large-file scan, and selected cache / log cleanup.

Actual platform results are recorded by the release tag's [workflow run](https://github.com/broruan/Clean_app/actions/workflows/build.yml); the target list above does not claim manual validation of every desktop.

### 简体中文

- 整理公开发布结构，提供英文 README、完整中文 README、贡献与安全指南，以及源码和验证文档。
- 更新原创 SVG 应用图标，在程序、桌面入口和文档中使用同一资源。
- 增加 Ubuntu 22.04、24.04、26.04 的 `amd64` / `arm64` 原生构建测试任务。
- 安装包名称包含发行版和架构，自动生成链接库依赖，汇总六个发布包与 `SHA256SUMS`。
- 保留现有中文界面，以及空间总览、软件库存、卸载预览、大文件扫描和所选缓存日志清理功能。

实际平台结果以对应标签的 [工作流记录](https://github.com/broruan/Clean_app/actions/workflows/build.yml) 为准，目标平台列表不代表所有桌面已完成人工验证。

## 1.1.1

### English

- Align cache-table checkbox painting with the visible interaction area under the production stylesheet.
- Support full-row clicks and Space to toggle cache selection.
- Keep selected space values readable when the table gains or loses focus.
- Add styled-widget regression coverage for those interactions.

### 简体中文

- 修复正式样式下缓存表格复选框可见位置与点击区域不一致。
- 支持整行点击与空格键切换缓存选择。
- 修复表格获取或失去焦点时选中空间数字的可读性。
- 增加对应正式样式界面回归测试。

[1.1.2]: https://github.com/broruan/Clean_app/releases/tag/v1.1.2
