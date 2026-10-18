# Validation scope / 验证范围

[Build instructions](building.md) · [CI runs](https://github.com/broruan/DiskLens/actions/workflows/build.yml)

## Version 1.3.0 / 版本 1.3.0

[Release CI #10](https://github.com/broruan/DiskLens/actions/runs/39109060208), tag `v1.3.0`, commit `e4a7b8f`: all six native combinations passed on 2026-10-18 — Ubuntu 22.04 / 24.04 / 26.04 × amd64 / arm64. Each ran all 13 CTest targets, verified and installed its DEB, checked all seven pages in both languages, and checked overview/cache/explorer/developer at English 18 px. The release completed in 2m 30s and published six DEBs plus SHA256SUMS. All six published downloads were independently verified against SHA-256 and version/architecture metadata. The same commit also passed the [main-branch run](https://github.com/broruan/DiskLens/actions/runs/39109059959).

2026-10-18，[发布 CI #10](https://github.com/broruan/DiskLens/actions/runs/39109060208) 在 `v1.3.0`、`e4a7b8f` 上完成六个原生平台检查：Ubuntu 22.04 / 24.04 / 26.04 × amd64 / arm64 全部通过。每个平台均执行 13 个 CTest 目标、DEB 检查与安装，以及七类页面的双语检查和总览、缓存、目录、开发页面的英文 18 px 检查。发布共用时 2 分 30 秒，生成六个 DEB 与 SHA256SUMS；发布后重新下载，六个包的 SHA-256、版本和架构全部正确。同一提交的 [主分支构建](https://github.com/broruan/DiskLens/actions/runs/39109059959) 也已通过。

Version 1.3.0 adds in-app English / Simplified Chinese switching, persistent font size preferences and session-only command-line overrides. Its targeted checks cover translation completeness and placeholders, preference persistence, production-style font scaling, visible language settings and safe appearance-change boundaries. The full suite now contains 13 CTest targets. Local Ubuntu 22.04 amd64 / Qt 5.15.3 checks passed all 13 targets, compiled translation coverage for 540 messages, and all seven pages in Chinese 13 px, English 13 px and English 18 px with isolated fixtures. The 1.2.0 results below remain historical evidence.

1.3.0 新增应用内中英文切换、字体大小偏好保存与仅本次启动的命令行覆盖。针对性检查覆盖译文完整性与占位符、偏好持久化、正式样式字体缩放、可见语言设置入口，以及外观切换的操作边界；完整测试现为 13 个 CTest 目标。本机 Ubuntu 22.04 amd64 / Qt 5.15.3 已通过全部 13 项、540 条编译译文检查，以及隔离样例下七类页面的中文 13 px、英文 13 px、英文 18 px 检查。下方 1.2.0 记录保留为历史证据。

## Version 1.2.0 / 版本 1.2.0

[Release CI #6](https://github.com/broruan/DiskLens/actions/runs/39090501349) passed on 2026-10-18 for tag `v1.2.0`, commit `ab2ade5`. **All six combinations passed**: Ubuntu 22.04, 24.04 and 26.04, each on amd64 and arm64. Each job ran all 11 CTest targets, verified and installed its DEB, and ran installed-app read-only smoke checks for overview, explorer, duplicates and developer pages. The release job published six DEBs and `SHA256SUMS`; all six published downloads were independently checked against their SHA-256 values and version/architecture metadata.

2026-10-18，[正式发布 CI #6](https://github.com/broruan/DiskLens/actions/runs/39090501349) 在标签 `v1.2.0`、提交 `ab2ade5` 上通过。Ubuntu 22.04、24.04、26.04 的 amd64 与 arm64 **六个组合全部通过**：各自完成 11 个 CTest 目标、DEB 检查、安装及四类新旧页面的安装后只读检查。发布任务生成六个安装包和校验文件；发布后重新下载六个包，SHA-256 与版本、架构元数据均验证正确。

Local Ubuntu 22.04 amd64 / Qt 5.15.3: all **11 CTest targets** passed, and the updated application passed read-only smoke checks for overview, cache, explorer, duplicates and developer pages using disposable fixtures. The previous 1.1.2 evidence below remains historical.

本机 Ubuntu 22.04 amd64 / Qt 5.15.3 的 **11 个 CTest 目标**全部通过，使用临时样例对总览、缓存、目录分析、重复文件和开发环境页面运行只读检查并截图。下方 1.1.2 记录保留为历史证据。

## Release v1.1.2 / 已验证发布

On 2026-10-18, [release CI run #5](https://github.com/broruan/DiskLens/actions/runs/39088717438) completed successfully for tag [`v1.1.2`](https://github.com/broruan/DiskLens/tree/v1.1.2), commit `eb7fa7a`. All six native jobs completed compilation, all five CTest targets, DEB verification, package installation, and a read-only offscreen smoke check. Release assets were downloaded again and all six DEBs matched `SHA256SUMS`.

2026-10-18，[正式发布 CI #5](https://github.com/broruan/DiskLens/actions/runs/39088717438) 在标签 `v1.1.2`、提交 `eb7fa7a` 上全部通过。六个原生任务均完成编译、五个 CTest 目标、DEB 检查、安装包安装与离屏只读界面检查；发布后重新下载六个 DEB，全部通过 `SHA256SUMS` 校验。

| Ubuntu | amd64 | arm64 |
| --- | --- | --- |
| 22.04 | Passed / 通过 | Passed / 通过 |
| 24.04 | Passed / 通过 | Passed / 通过 |
| 26.04 | Passed / 通过 | Passed / 通过 |

These are automated CI results; the desktop and device boundaries below still apply. / 以上为自动化 CI 结果，下述桌面与实机验证边界仍然适用。

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
| `spaceanalyzer` / `explorerpage` | Directory aggregation and navigation, complete duplicate verification beyond matching samples, changing-file rejection, resource limits, cancellation, reports and production-style interaction. |
| `developer` / `developerpage` | Structural classification, raw-data descriptions, nested/hard-link accounting, partial reports and production-style filtering/selection. |
| `appstorage` / `appstoragedialog` | Exact namespaces/catalog, separately measured data, unknown association and styled read-only inspection. |
| `appearance` / `localization` | Language resources and placeholder integrity, saved preferences, font scaling and production-style appearance controls. See the exact release run for completion and skips. |
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
| `spaceanalyzer` / `explorerpage` | 目录累加与逐层导航、采样碰撞后的完整哈希、变化文件排除、上限、取消、报告及正式样式交互。 |
| `developer` / `developerpage` | 结构分类、原始数据说明、嵌套与硬链接计量、部分报告及正式样式筛选选择。 |
| `appstorage` / `appstoragedialog` | 精确命名空间和目录规则、独立计量、未知关联说明及只读界面。 |
| `appearance` / `localization` | 语言资源与占位符完整性、偏好保存、字体缩放及正式样式外观控件；完成结果与跳过项以对应发布记录为准。 |
| 安装包检查 | Debian 元数据、架构、可执行文件、桌面入口、图标、依赖声明与产物命名。 |
| CI 安装与只读界面检查 | 在 CI 环境安装生成包，再对临时目录运行离屏只读检查。 |

缓存删除测试仅操作临时目录。只读检查模式不触发清理、卸载或管理员授权。CI 库存来自构建机，不代表用户桌面软件环境。

### 需要独立验证的内容

CI 通过不能证明所有桌面会话、Polkit 代理、显示服务器、GPU、文件系统、Snap / Flatpak 安装或 ARM 实机的行为。真实授权后的 DEB / Snap / Flatpak 卸载、桌面菜单与图标渲染、真实缓存清理流程，应在可丢弃环境分别验证后再报告。

上述测试不构成真实用户缓存清理或授权卸载验证。发布说明应分别列出自动结果与手工桌面验证。

### 如何记录验证

附上标签或提交、Ubuntu 版本、架构、Qt 版本、命令、总体结果和跳过项。例如“Ubuntu 22.04 amd64 / Qt 5 下五个 CTest 目标通过”具有明确范围，不能扩大为“所有桌面与架构已验证”。
