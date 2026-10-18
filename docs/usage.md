# User guide / 使用指南

[English README](../README.md) · [中文 README](../README.zh-CN.md)

## English

The current interface is Simplified Chinese. The navigation labels below match the application so you can find each view.

### Storage overview — 空间总览

Choose a mounted volume to inspect filesystem totals. Used space is total capacity minus free space; available space excludes filesystem reservations. Installed-package estimates and scanned files can belong to other mounts, so they are not slices of the volume's usage chart.

The application starts a background, read-only scan of your home directory with a 100 MiB threshold. You can continue using other pages or stop the scan. A canceled scan is marked as partial.

### Installed applications — 已安装软件

The default filter shows packages with desktop entries. Switch to **所有已安装软件包** to include libraries, runtimes, and other packages. Search matches name, package identifier, or description; source filters select DEB, Snap, or Flatpak. Click a column header to sort.

Manually installed software and AppImages without package-manager records are not listed. Snap and Flatpak inventory is available when their tools and installations are present; DiskLens does not install those package managers for you.

Package sizes are estimates, not a promise of recoverable disk space:

| Source | Measurement | Limitations |
| --- | --- | --- |
| DEB | `dpkg` `Installed-Size`, converted from KiB | Excludes user files, caches, and files outside package records; invalid or absent sizes display `—`. |
| Snap | Compressed `.snap` file for the current revision | Excludes writable data, older revisions, and automatic snapshots. |
| Flatpak | Deployment size from `flatpak list` | Excludes user data; shared objects may be counted more than once. |

The overview's software total includes all inventoried packages; its application ranking shows desktop applications.

### Uninstalling an application

1. Select an application and start its removal preview.
2. Read the proposed affected packages and source-specific notes.
3. Confirm only if you understand the changes. Privileged operations use the system's Polkit dialog; DiskLens does not collect or store your password.
4. Wait for the package-manager transaction to finish. The application refuses to close while removal is executing.

DEB removal runs an APT simulation, checks target and affected-package metadata, and repeats validation immediately before execution. Essential / Protected, required / important, and selected desktop, network, boot, and package-management components are blocked. DiskLens runs `remove`, without `purge` or `autoremove`. Avoid running other package managers concurrently: preview and execution do not form an atomic transaction.

Snap does not provide an equivalent simulated removal plan. DiskLens verifies the installed application and blocks core / base / runtime components; the confirmation explains this limitation. Normal Snap removal behavior may create automatic data snapshots.

Flatpak removes the selected complete application reference from its user or system installation with `--app --no-related`. It does not request removal of related runtimes or application user data.

### Large files — 大文件

Choose a directory and a threshold of 10 MiB, 100 MiB, 500 MiB, or 1 GiB. Start, stop, or repeat a scan. The table retains the largest 500 matching files; totals include all matched files, even those outside the table. You can filter results, copy a path, and open a containing folder. There is no delete operation on this page.

The large-file scan reads path and file metadata, not file contents. The separate duplicate checker reads candidate contents after explicit activation; developer classification reads small marker files. Hidden files are included. Symlinks, special files, and other mounts are skipped; hard links are counted once. The root path and its ancestors must not contain symlinks. Permission failures, inaccessible paths, and changing entries can produce skipped items or partial results. Sparse files have distinct logical and allocated sizes.

### Caches & logs — 缓存与日志

The first visit starts a scan. Ordinary caches and logs are grouped separately by application and sorted by allocated bytes. The defaults are:

| Root | Included files |
| --- | --- |
| `~/.cache` | Ordinary cache files, with logs shown separately |
| `~/.local/state` | Only `.log`, `.log.*`, and files below `log` / `logs` directories |

Absolute `XDG_CACHE_HOME` and `XDG_STATE_HOME` values override the defaults. The entire state directory is not treated as cache. The backend rejects broad system directories, the whole home directory and its ancestors, and overlapping roots. Cleanup does not request administrator privileges.

No groups are selected by default. Click a row or its checkbox, or press Space on the current row, to toggle it. You can search, filter to allocated size **greater than 100 MiB**, or show only logs. **全选当前筛选** selects visible filtered groups. Existing selections remain selected when the filter changes; inspect the selection summary or use **清空选择** before continuing.

**一键清理所选** opens a confirmation listing groups, paths, files, and estimated allocated size. Files are permanently deleted, without the trash. Close the related applications first; caches can be recreated and downloads may be needed again. Log deletion also removes those records.

Only selected regular files from the scan snapshot are considered. Roots, ancestors, ownership, mount boundaries, file identity, sizes, and timestamps are rechecked before unlinking. Newly created, changed, replaced, foreign-owned, or multiply linked files are retained. Directories are retained. Stop requests prevent later deletions, but do not undo completed ones. Results show removed, skipped, and failed files, then the page scans again.

These checks are not a security isolation boundary against a malicious process running as your own user: POSIX has no atomic “delete only this inode” operation, leaving a small check-to-delete race. See [SECURITY.md](../SECURITY.md).

Allocated-byte totals come from file block counts. They do not guarantee an equal immediate increase in available disk space; for example, another process may still hold a deleted file open. Refresh the volume view to inspect actual available space.

## 简体中文

### 空间总览

选择挂载卷查看文件系统容量。已用空间 = 总容量 − 空闲空间；“可用”空间扣除了文件系统预留。软件与扫描文件可能位于其他挂载卷，不参与环形图分类。

程序启动后会在后台以 100 MiB 为阈值只读扫描主目录。期间可以切换页面或停止扫描；中止后的结果会标记为部分结果。

### 已安装软件

默认展示有桌面入口的软件，切换 **所有已安装软件包** 可查看库、运行时和其他组件。支持名称、包名、描述搜索，DEB / Snap / Flatpak 来源筛选，以及点击表头排序。

没有软件包管理器记录的手工安装程序和 AppImage 不在列表中。Snap / Flatpak 库存依赖本机已有的命令与安装环境，DiskLens 不会替你安装这些包管理器。

软件包体积是估算值，不等于卸载可释放的空间：

| 来源 | 体积依据 | 限制 |
| --- | --- | --- |
| DEB | `dpkg` 的 `Installed-Size`，KiB 转为字节 | 不含用户文件、缓存及包记录之外的数据；字段缺失或无效时显示 `—`。 |
| Snap | 当前修订的 `.snap` 压缩包大小 | 不含可写数据、旧修订与自动快照。 |
| Flatpak | `flatpak list` 的部署体积 | 不含用户数据；共享对象可能重复计算。 |

总览的软件体积统计全部已识别软件包，软件排行只展示桌面应用。

### 卸载软件

1. 选择应用并分析卸载范围。
2. 阅读受影响软件包列表与该来源的提示。
3. 理解变化后再确认。需要权限时由系统 Polkit 窗口授权，DiskLens 不收集或保存密码。
4. 等待包管理事务结束。程序会在卸载执行期间阻止关闭窗口。

DEB 先运行 APT 模拟，检查目标与受影响软件包元数据，在执行前再次校验。会阻止 Essential / Protected、required / important，以及部分桌面、网络、启动与包管理关键组件的卸载。只运行 `remove`，不运行 `purge` 或 `autoremove`。预览和执行并非原子事务，请避免同时运行其他包管理工具。

Snap 没有等效的卸载模拟计划。程序会检查已安装应用并阻止核心、基础和运行时组件，确认窗口会说明限制。Snap 默认卸载行为可能生成自动数据快照。

Flatpak 通过 `--app --no-related` 卸载用户或系统安装范围内的所选完整应用引用，不主动删除关联运行时与应用用户数据。

### 大文件

选择目录与 10 MiB、100 MiB、500 MiB、1 GiB 阈值，开始、停止或重新扫描。表格最多保留最大的 500 个匹配文件，总数和总逻辑体积包含全部匹配项。可筛选文件、复制路径、打开所在目录，此页没有删除功能。

大文件扫描只检查路径与文件元数据，不读取内容。独立的重复检测在显式启动后读取候选文件内容；开发目录识别会限量读取标记文件。包含隐藏文件，跳过软链接、特殊文件和其他挂载点，硬链接只计一次。扫描根路径及其祖先不能包含软链接。权限不足、路径不可访问或条目变化可能导致跳过项或部分结果。稀疏文件的逻辑大小与实际分配空间不同。

### 缓存与日志

首次进入会自动扫描。按应用分别汇总普通缓存与日志，以实际分配空间降序排列。默认范围：

| 根目录 | 扫描范围 |
| --- | --- |
| `~/.cache` | 普通缓存文件，日志单独分组 |
| `~/.local/state` | 仅 `.log`、`.log.*`，或 `log` / `logs` 目录中的文件 |

绝对路径的 `XDG_CACHE_HOME` / `XDG_STATE_HOME` 可覆盖默认值。不会将整个状态目录视为缓存。后端拒绝系统公共目录、整个主目录及其祖先，以及相互包含的根目录。缓存清理不申请管理员权限。

默认没有条目被勾选。点击整行、复选框，或对当前行按空格键，可切换勾选状态。支持搜索、筛选实际占用 **大于 100 MiB** 的条目、仅看日志。**全选当前筛选** 只添加当前可见分组；切换筛选后原有选择仍会保留，请检查选择汇总，必要时先 **清空选择**。

**一键清理所选** 会列出分组、路径、文件数与预计实际占用。文件直接删除，不进入回收站。建议先关闭相关应用；缓存可能重新生成或需要重新下载，日志删除后对应记录也会消失。

仅处理扫描快照中勾选分组的普通文件。删除前会核对根目录、祖先目录、所有权、挂载边界、文件身份、大小与时间戳。新建、修改、替换、不属于当前用户、含多个硬链接的文件均保留，目录也保留。停止操作只阻止后续删除，无法撤销已经完成的删除。结束后显示成功、跳过与失败数量，并重新扫描。

这些检查不能作为对抗同一用户身份下恶意进程的安全隔离边界：POSIX 没有按 inode 条件原子删除的接口，检查与删除之间仍有很小的竞争窗口。详见 [SECURITY.md](../SECURITY.md)。

实际占用与删除占用来自文件分配块统计，不保证可用空间立即增加相同数值，例如文件仍被其他进程打开时。请刷新磁盘视图查看实际可用空间。

## New analysis tools / 新分析工具

Version 1.2 adds the **空间分析** and **开发环境** pages, plus **空间画像** on the installed-app page. See [the bilingual analysis guide](analysis-tools.md) for exact workflows, evidence rules and accounting. These views inspect and export; they do not delete duplicate files, developer outputs or application data.

1.2 新增 **空间分析**、**开发环境** 页面，以及软件列表中的 **空间画像**。具体操作、识别证据和统计口径见 [双语分析指南](analysis-tools.md)。这些功能用于查看和导出，不删除重复文件、开发产物或软件用户数据。
