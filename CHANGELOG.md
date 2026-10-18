# Changelog / 更新记录

## 1.3.0 — 2026-10-18

### English

- Switch the entire interface between English and Simplified Chinese from Language & font.
- Adjust the base font size from 11 to 18 px, with scaled text in tables, charts and dialogs.
- Save preferences and apply them immediately while preserving the page, scan directories and window position; restart read-only scans with fresh selections.
- Guard appearance changes during cache cleanup, package operations and modal dialogs.
- Add non-persistent `--language` and `--font-size` overrides and translation/persistence/production-style GUI tests.
- Rename the canonical repository to `broruan/DiskLens`; separate English and Chinese release notes and README introductions.

### 简体中文

- 在「语言与字体」中切换完整英文、简体中文界面。
- 基础字号可设为 11–18 px，表格、图表与对话框文字同步缩放。
- 设置自动保存、立即生效，保留当前页面、扫描目录和窗口位置；只读扫描重新开始，清空待处理选择。
- 清理、软件包操作和模态窗口期间阻止切换，避免中断任务。
- 增加不保存的 `--language`、`--font-size` 覆盖，以及翻译、设置持久化与正式样式界面测试。
- 仓库地址改为 `broruan/DiskLens`，发布说明与 README 简介按中英文分开。


## 1.2.0 — 2026-10-18

### English

- Added interactive directory hierarchy and a clickable current-level allocation chart, with logical/allocated sizes, hard-link deduplication and explicit partial results.
- Added explicit duplicate detection using size, head/tail samples and complete SHA-256; reports contain grouped paths and logical redundancy without deleting files.
- Added developer storage classification for colcon outputs, ROS recordings, point clouds, pip/npm/ccache caches and Conda environments, with evidence and rebuildability explanations.
- Added application storage attribution for known DEB directories and Snap/Flatpak namespaces, separately measured from package estimates.
- Added atomic CSV reports with scope, completeness and spreadsheet-formula protection; existing scanned inputs cannot be overwritten by export.
- Expanded automated tests and installed-package smoke checks for the new views.

### 简体中文

- 新增逐层目录分析、可点击占用比例图、逻辑与实际大小及部分结果说明。
- 新增显式完整内容重复校验、分组路径及逻辑冗余报告，不自动删除。
- 新增开发目录识别、结构证据及用途和可重建性说明。
- 新增软件空间画像，分别显示包估算与关联用户目录。
- 新增原子 CSV 导出、扫描范围和完整性标记及公式保护，阻止覆盖已扫描输入。
- 扩展自动测试和安装后新页面检查。

## [1.1.2] - 2026-10-18

### English

- Prepare public distribution with an English README, a complete Chinese README, contribution and security guides, and source / validation documentation.
- Refresh the original SVG application icon and use the same asset in the application, desktop entry, and documentation.
- Add native build and test jobs targeting Ubuntu 22.04, 24.04, and 26.04 on `amd64` and `arm64`.
- Generate release-specific `.deb` names and shared-library dependencies, with six-package release aggregation and `SHA256SUMS`.
- Keep the existing Chinese interface and its storage overview, package inventory, uninstall preview, large-file scan, and selected cache / log cleanup.

Actual platform results are recorded by the release tag's [workflow run](https://github.com/broruan/DiskLens/actions/workflows/build.yml); the target list above does not claim manual validation of every desktop.

### 简体中文

- 整理公开发布结构，提供英文 README、完整中文 README、贡献与安全指南，以及源码和验证文档。
- 更新原创 SVG 应用图标，在程序、桌面入口和文档中使用同一资源。
- 增加 Ubuntu 22.04、24.04、26.04 的 `amd64` / `arm64` 原生构建测试任务。
- 安装包名称包含发行版和架构，自动生成链接库依赖，汇总六个发布包与 `SHA256SUMS`。
- 保留现有中文界面，以及空间总览、软件库存、卸载预览、大文件扫描和所选缓存日志清理功能。

实际平台结果以对应标签的 [工作流记录](https://github.com/broruan/DiskLens/actions/workflows/build.yml) 为准，目标平台列表不代表所有桌面已完成人工验证。

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

[1.1.2]: https://github.com/broruan/DiskLens/releases/tag/v1.1.2
