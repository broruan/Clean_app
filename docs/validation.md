# Validation scope / 验证范围

[Build instructions](building.md) · [CI runs](https://github.com/broruan/Clean_app/actions/workflows/build.yml)

## English

The release workflow targets six native builds: Ubuntu 22.04, 24.04, and 26.04, each on `amd64` and `arm64`. The workflow definition is a target matrix; a successful run for the exact release tag is the evidence that its jobs passed. Read individual test logs for conditional skips. Do not infer a green result from the presence of this document or a downloadable package alone.

### Local verification recorded on 2026-10-18

For version 1.1.2 on Ubuntu 22.04 `amd64` with Qt 5, a fresh release build passed all five CTest targets. The generated package was unpacked and checked for metadata, ELF architecture, dependency declarations, desktop / icon assets, checksums, and application version. Read-only offscreen smoke checks passed for the overview and cache pages, producing the README screenshots from disposable demonstration directories. This local record does not claim that the package was installed on every target or that removal / real-cache cleanup was executed.

### What automated checks establish

| Check | Scope |
| --- | --- |
| `scanner` | Thresholds, largest-file limits, hidden files, links, sparse files, mount / permission handling, and cancellation in controlled fixtures. |
| `inventory` | Installed package queries, size and desktop metadata handling, and Flatpak parsing. Optional integrations depend on the host environment. |
| `uninstall` | Identifier validation, protected-component checks, preview lifecycle, and harmless read-only APT simulation where its fixture application exists. It does not perform real uninstall. |
| `cache` | Cache / log classification; selected-file deletion in temporary directories; unchanged-file checks; links, replacement, ownership, mount / root and manifest validation; cancellation. |
| `cachepage` | Production Fusion style and stylesheet; visible checkbox and whole-row clicks; Space selection; focused / unfocused selection contrast; filtering, confirmation, temporary-file deletion, and rescan. |
| Package checks | Debian metadata, declared architecture, bundled executable / desktop entry / icon, generated dependency declarations, and artifact naming. |
| CI package installation and smoke | Installation in the CI environment followed by a read-only offscreen UI check over a temporary directory. |

Cache deletion fixtures use temporary directories. Read-only smoke mode does not trigger cleanup, uninstall, or administrator authorization. The CI inventory includes its runner's software, rather than a representative end-user desktop.

### What needs desktop or device validation

A green CI run does not establish correct behavior of every desktop session, Polkit agent, display server, GPU, filesystem, Snap / Flatpak installation, or physical ARM device. Real authorized DEB / Snap / Flatpak removal, desktop menu and icon rendering, and real cache-cleanup workflows must be checked separately in disposable environments before reporting them as tested.

No claim of real user-cache deletion or authorized package removal is made by the tests above. The latest release notes should distinguish automated results from manual desktop checks.

### Reporting results

Include the release tag or commit, Ubuntu version, architecture, Qt version, exact command, overall result, and skipped cases. A report such as “all five CTest targets passed on Ubuntu 22.04 amd64 with Qt 5” is scoped evidence. It should not be expanded into “all desktops and architectures tested.”

## 简体中文

发布工作流面向六个原生构建组合：Ubuntu 22.04、24.04、26.04，各自包含 `amd64` 与 `arm64`。工作流定义代表目标矩阵，具体发布标签的实际成功记录才是通过证据。应查看单项测试日志里的条件跳过说明；不能仅凭本文存在或有可下载包就推断 CI 已通过。

### 2026-10-18 本机验证记录

1.1.2 在 Ubuntu 22.04 `amd64` / Qt 5 下完成全新 Release 构建，五个 CTest 目标全部通过。生成包已解包检查元数据、ELF 架构、依赖声明、桌面与图标资源、校验和及程序版本。总览和缓存页面的离屏只读检查通过，README 截图来自可丢弃演示目录。此记录不代表已在所有目标系统安装，不包含实际卸载或真实用户缓存删除。

### 自动检查覆盖

| 检查 | 范围 |
| --- | --- |
| `scanner` | 受控样例中的阈值、Top-N、隐藏文件、链接、稀疏文件、挂载与权限处理、取消。 |
| `inventory` | 本机库存、体积与桌面元数据、Flatpak 解析；可选集成依赖测试环境。 |
| `uninstall` | 非法标识、系统组件保护、预览生命周期、存在样例应用时的无害 APT 只读模拟；不实际卸载。 |
| `cache` | 缓存日志分类、临时目录内所选文件删除、文件变化复核、链接与替换、所有权、挂载与根目录、快照校验、取消。 |
| `cachepage` | 正式 Fusion 风格与完整样式；可见复选框、整行和空格勾选；有无焦点时选中数字对比度；筛选、确认、临时文件删除与重扫。 |
| 安装包检查 | Debian 元数据、架构、可执行文件、桌面入口、图标、依赖声明与产物命名。 |
| CI 安装与只读界面检查 | 在 CI 环境安装生成包，再对临时目录运行离屏只读检查。 |

缓存删除测试仅操作临时目录。只读检查模式不触发清理、卸载或管理员授权。CI 库存来自构建机，不代表用户桌面软件环境。

### 需要独立验证的内容

CI 通过不能证明所有桌面会话、Polkit 代理、显示服务器、GPU、文件系统、Snap / Flatpak 安装或 ARM 实机的行为。真实授权后的 DEB / Snap / Flatpak 卸载、桌面菜单与图标渲染、真实缓存清理流程，应在可丢弃环境分别验证后再报告。

上述测试不构成真实用户缓存清理或授权卸载验证。发布说明应分别列出自动结果与手工桌面验证。

### 如何记录验证

附上标签或提交、Ubuntu 版本、架构、Qt 版本、命令、总体结果和跳过项。例如“Ubuntu 22.04 amd64 / Qt 5 下五个 CTest 目标通过”具有明确范围，不能扩大为“所有桌面与架构已验证”。
