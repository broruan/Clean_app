# Contributing / 参与贡献

## English

Bug reports, focused fixes, documentation improvements, and platform validation are welcome. Please search [existing issues](https://github.com/broruan/DiskLens/issues) before opening a new one. Use [SECURITY.md](SECURITY.md) for vulnerability reports.

### Before changing code

1. Describe the user-visible problem and a reproducible example. Include Ubuntu version, `dpkg --print-architecture`, DiskLens version, and package source.
2. Read the relevant module and its tests; the [architecture guide](docs/architecture.md) maps the source tree.
3. Keep a change focused. Discuss new destructive operations, privilege boundaries, or package-manager behavior in an issue before a large implementation.

### Build and verify

Follow [docs/building.md](docs/building.md). The release toolchain is C++17 with Qt 5; Qt 6 is an explicit source-build option. The application and documentation support English and Simplified Chinese.

For a code change, compile and run the relevant CTest targets. Run the complete suite before proposing a release. UI changes need review using the production Fusion style and `applicationStyle()`, including a screenshot when visible behavior changes. Cache table changes should verify visible checkbox clicks, whole-row clicks, Space toggling, and focused / unfocused text contrast. A model-only test is insufficient for those interactions.

Use `QTemporaryDir` or another explicitly disposable fixture for deletion tests. Do not use your real cache or an installed application as a destructive test fixture. Existing uninstall integration tests use read-only package queries and APT simulation. Report skipped cases and keep automated results separate from real-desktop validation.

### Implementation expectations

- Keep filesystem and package-manager work outside the UI thread; make long-running read-only work cancelable where practical.
- Pass process arguments separately through `QProcess`. Do not build shell commands from paths, package identifiers, or metadata.
- Preserve the separation between read-only inspection, explicit confirmation, and execution.
- Preserve ownership, mount, symlink, snapshot, and protected-package checks. Changes to these boundaries need a regression case demonstrating the relevant failure or risk.
- Do not equate package estimates or file allocated-byte totals with guaranteed recovered space.
- Wrap user-visible source text in `dlTr()` and update `translations/disklens_en.ts`. Run `python3 tools/update-translations.py --compile` after installing the developer-only `qttools5-dev-tools`; commit both the `.ts` and compiled `.qm`. Preserve placeholders, markup, line breaks and intentional whitespace.
- Check changed views in English and Chinese at default and larger font sizes using the production stylesheet. Verify that controls remain readable and usable.
- Keep English and Chinese README information consistent, especially filenames, dependencies, supported targets, and validation claims.

### Pull requests

Explain the concrete problem, resulting behavior, and checks you actually ran. Include the environment and any skips or remaining limits. Update relevant documentation and `CHANGELOG.md` when user-visible behavior changes. Avoid unrelated formatting and generated build / package files. Submitting a contribution means you agree that your contribution is available under the project's MIT license; third-party material must retain its own license and attribution.

## 简体中文

欢迎问题反馈、聚焦的修复、文档改进与平台验证。提交前请搜索 [已有问题](https://github.com/broruan/DiskLens/issues)。安全漏洞请按 [SECURITY.md](SECURITY.md) 报告。

### 修改前

1. 描述用户可见的问题与复现步骤，提供 Ubuntu 版本、`dpkg --print-architecture`、DiskLens 版本与安装来源。
2. 阅读对应模块和测试，[源码结构](docs/architecture.md) 可用于定位。
3. 保持改动聚焦。新增破坏性操作、权限边界或包管理器行为前，请先通过 issue 讨论大范围实现。

### 构建与验证

按 [构建指南](docs/building.md) 操作。发布工具链为 C++17 / Qt 5，Qt 6 是显式的源码构建选项。应用与仓库文档均支持简体中文和英文。

代码改动应编译并执行相关 CTest；发布前运行完整测试。界面改动应使用正式 Fusion 风格与 `applicationStyle()` 检查，可见行为变化请附截图。缓存表格变动应验证复选框可见区域、整行点击、空格切换，以及有无焦点时的选中文字对比度；只测模型状态无法覆盖这些交互。

删除测试请使用 `QTemporaryDir` 或明确可丢弃的样例，不要把真实缓存或已安装应用作为破坏性测试对象。已有卸载集成测试只进行只读查询与 APT 模拟。请报告跳过项，区分自动测试与实机桌面验证。

### 实现约定

- 文件系统和包管理任务放在界面线程之外；较长的只读任务尽可能支持协作取消。
- 通过 `QProcess` 独立传递参数，不把路径、包名或元数据拼接成 shell 命令。
- 保持只读检查、明确确认与执行之间的边界。
- 保留所有权、挂载、软链接、快照与系统组件保护；修改边界时，应补充针对该故障或风险的回归用例。
- 不把软件包估算大小或分配块统计称为保证释放的磁盘空间。
- 面向用户的源码文本使用 `dlTr()`，同步更新 `translations/disklens_en.ts`。安装仅开发需要的 `qttools5-dev-tools` 后运行 `python3 tools/update-translations.py --compile`，同时提交 `.ts` 与编译后的 `.qm`；保留占位符、标记、换行及有意使用的空白。
- 使用正式样式检查改动页面的中英文默认字号与较大字号，确认文字、控件均可读可用。
- 保持中英文 README 的包名、依赖、目标平台与验证表述一致。

### 提交 PR

说明具体问题、改动后的行为与实际检查结果，附上环境、跳过项和剩余限制。用户可见行为变化时更新相关文档与 `CHANGELOG.md`。避免无关格式改动，以及构建和打包产物。提交贡献表示同意将贡献按本项目 MIT 许可证发布；第三方内容应保留原许可证与归属声明。
