# 空间分析与开发环境指南 / Analysis tools guide

适用于 DiskLens 1.2.0。新加入的「空间分析」「开发环境」和「软件空间画像」均为只读分析：可以查看、打开目录和复制路径；空间分析与开发环境还支持导出报告。这些页面不提供删除按钮，也不会执行构建、卸载或缓存清理命令。

For DiskLens 1.2.0. **Space Analysis**, **Developer Environment**, and **Application Storage Profile** are read-only tools. They inspect storage, open directories, and copy paths. Space Analysis and Developer Environment also export reports. These pages do not delete files or run build, uninstall, or cache-cleaning commands.

## 1. 从目录开始 / Start with a directory

1. 打开「空间分析」，选择要检查的真实目录。进入页面会启动目录元数据统计；修改路径后点击「重新扫描」。
2. 查看「目录空间分析」页的实际占用、逻辑大小和文件数。双击目录行，或点击右侧比例图中的目录行，即可进入下一层；用上一级或根目录按钮返回。
3. 使用搜索定位名称或路径。比例图显示当前层匹配的前 8 项，左侧列表保留其余匹配项；细小条目也能点击整行进入。
4. 选中条目后复制完整路径，或打开其所在目录；需要保存结果时导出当前页的 CSV。

1. Open **Space Analysis** and choose a real directory. Opening the page starts a metadata scan; after editing the path, select **Rescan**.
2. Inspect allocated bytes, logical bytes, and file counts. Double-click a directory row, or activate its row in the chart, to move down a level. Use the parent/root controls to return.
3. Search names or paths. The chart shows the largest eight matching children of the current directory; the table retains the remaining matches. The entire chart row is clickable, including tiny entries.
4. Select an entry to copy its full path or open its directory. Export the active tab to CSV when a saved snapshot is useful.

比例图的分母始终是**当前目录的实际占用总量**，不会将搜索后的剩余项目重新归一化为 100%。它只展示当前层前 8 项，目录自身元数据也占空间，因此可见条目的比例之和不一定等于 100%。图表支持方向键选择、Enter / Space 激活。

The chart denominator is always the **current directory's total allocated bytes**. Search results are not renormalized to 100%. Only eight children are charted, and directory metadata also occupies blocks, so visible percentages need not sum to 100%. Arrow keys select chart rows; Enter or Space activates them.

## 2. 正确理解大小 / Understand the metrics

| 指标 / Metric | 含义 / Meaning |
| --- | --- |
| 逻辑大小 / Logical bytes | 普通文件长度之和，即 `st_size`；稀疏文件可能很大，但实际分配很少。Sum of regular-file lengths (`st_size`); sparse files may have a large length and little allocation. |
| 实际占用 / Allocated bytes | `st_blocks × 512`。空间分析和软件画像还包含已扫描目录自身的分配块；开发环境只统计普通文件块。`st_blocks × 512`. Space Analysis and application profiles include scanned directory blocks; Developer Environment counts regular-file blocks only. |
| 文件数 / File count | 已统计的唯一普通文件 inode 数，不包含目录或硬链接的重复别名。Unique counted regular-file inodes, excluding directories and additional hard-link names. |
| 逻辑冗余量 / Logical redundancy | 重复组内 `(文件数 − 1) × 单文件逻辑大小`。For each duplicate group, `(file count − 1) × logical bytes per file`. |

同一设备与 inode 的硬链接只计首次扫描路径，其他别名不重复显示或计量。跨目录硬链接的归属取决于遍历顺序。reflink、快照或文件系统共享数据块没有按物理共享关系去重；以上数值均不能直接换算为删除后释放的空间。软件包元数据记录的安装体积也不能与用户目录统计直接相加。

Hard links sharing a device and inode are represented by the first encountered path. Other names are omitted rather than counted again; attribution across directories depends on traversal order. Reflinks, snapshots, and shared filesystem extents are not deduplicated by physical extent. These figures do not promise reclaimable space. Package-manager installation estimates must not be added directly to user-directory measurements.

## 3. 按需查找重复文件 / Find duplicates explicitly

切换到重复文件页后，选择最小文件大小并点击「查找重复文件」才会读取候选内容。单纯打开页面、切换标签、搜索或修改阈值不会自动进行完整哈希；修改阈值后需重新点击查找。CLI 的 `--page duplicates` 同样表示显式请求重复扫描。

On the duplicate tab, choose a minimum size and select **Find Duplicates** to read candidate contents. Opening the page, switching tabs, searching, or changing the threshold does not start content hashing. Run the scan again to apply a changed threshold. The CLI option `--page duplicates` is also an explicit duplicate-scan request.

扫描分三个阶段：

1. **按逻辑大小分组**：只有至少两个独立 inode 的同尺寸文件才进入后续阶段；即使最小值为 0，也排除空文件。
2. **采样 SHA-256 筛选**：超过 128 KiB 的文件读取头、尾各 64 KiB；较小文件读取全部内容。采样一致只表示候选，不能证明内容相同。
3. **完整 SHA-256 校验**：对采样相同的候选读取全部内容，完整哈希相同才形成重复组；相同大小和头尾、但中间不同的文件不会被归为同组。

The scan has three stages:

1. **Group by logical size**: only equal-size files with distinct inodes become candidates. Empty files are excluded even with a zero threshold.
2. **Sample SHA-256**: files larger than 128 KiB are sampled at the first and last 64 KiB; smaller files are read in full. A matching sample only identifies candidates.
3. **Full SHA-256**: remaining candidates are read completely. Only matching complete hashes form a group; different middle content is detected even when size and samples match.

内容读取前后核对设备、inode、长度、mtime 和 ctime；发布分组前再次复核成员。读取过程中变化、被替换或无法读取的文件会排除并计入跳过项。读取以 1 MiB 块进行，块间检查取消请求。结果是扫描时的快照，不持续监控文件。列表没有“默认保留项”或自动删除选择，逻辑冗余量不代表可以回收的磁盘块。

Device, inode, size, mtime, and ctime are checked before and after content reads, and group members are checked again before publication. Changed, replaced, or unreadable files are excluded and counted as skipped. Reads use 1 MiB blocks with cancellation checks between blocks. Results are snapshots rather than continuous monitoring. There is no default keeper or automatic deletion selection; logical redundancy is not reclaimable block space.

## 4. 扫描范围与部分结果 / Scope and partial results

空间分析、重复检测和软件画像只在所选目录的挂载范围内工作，不提权、不跟随符号链接，也拒绝根路径祖先中的符号链接。目录通过 Linux 文件描述符逐段打开，挂载 ID 与挂载表用于检查边界，包括同设备的 bind mount。FIFO、设备、套接字等特殊文件不会作为普通文件读取。

Space Analysis, duplicate detection, and application profiles stay on the selected root mount. They do not elevate privileges or follow symbolic links, including links in the root path's ancestors. Directories are opened component by component using Linux file descriptors; mount IDs and mount information enforce boundaries, including same-device bind mounts. FIFOs, devices, sockets, and other special files are not read as regular files.

| 模块 / Tool | 默认资源上限 / Default limits |
| --- | --- |
| 目录分析 / Directory analysis | 100,000 个节点，包含根、子目录和普通文件；最多 128 层目录。100,000 nodes including the root, directories, and regular files; at most 128 directory levels. |
| 重复检测 / Duplicate detection | 100,000 个唯一普通文件，包含低于最小大小的文件；另限制最多 100,000 个目录；最多 128 层。100,000 unique regular files, including files below the size threshold; a separate 100,000-directory limit; at most 128 levels. |
| 软件画像 / Application profile | 每个关联目录使用目录分析的上述上限。The directory-analysis limits apply separately to each associated location. |
| 开发环境 / Developer Environment | 最多检查 500,000 项、50,000 个目录、32 层；显示和导出实际占用最大的 2,000 组。At most 500,000 inspected entries, 50,000 directories, and 32 levels; display/export the largest 2,000 groups by allocated bytes. |

目录与重复后端的数量参数允许范围为 1 至 1,000,000；界面使用上述默认值。达到数量或深度上限、取消扫描、权限/挂载/读取错误会以部分结果或错误呈现，不能将未扫描内容当作 0。空间分析中符号链接和特殊文件属于固定排除范围，不计入 `skippedEntries`；开发环境另行记录跳过的符号链接。开发环境达到显示组数上限时，总计仍包含未显示的组，报告注明省略数量。

The directory and duplicate backends accept count limits from 1 to 1,000,000; the UI uses the defaults above. Cancellation, count/depth limits, and permission, mount, or read failures produce partial results or errors. Unscanned content is not zero usage. In Space Analysis, symbolic links and special files are fixed exclusions rather than `skippedEntries`; Developer Environment records skipped symbolic links separately. When the developer display limit is reached, totals still include hidden groups and the report records the omission count.

## 5. 识别开发环境 / Identify developer storage

打开「开发环境」，选择整个工作空间或包含相关目录的上级目录，等待统计完成，然后按类别或名称筛选。选择一行可查看**识别依据、用途、可重建性和建议动作**。结构规则用于解释用途；“通常可重建”依赖源码、依赖、构建参数、软件源和工具链等条件，不表示可以直接删除。

Open **Developer Environment**, choose a workspace or a parent containing the relevant directories, then filter by category or name. Selecting a row shows **evidence, purpose, rebuildability, and suggested action**. Structural rules explain likely use. “Usually rebuildable” depends on source code, dependencies, build settings, repositories, and toolchains; it is not permission to delete.

| 类别 / Category | 识别证据 / Structural evidence |
| --- | --- |
| ROS 构建产物 / ROS build | `build/.built_by` 内容为 `colcon`，或 `build/<包>/colcon_command_prefix_build.sh`。只有 `src` 内的 `package.xml` 不足以把任意同名目录视作构建产物。`.built_by` containing `colcon`, or an immediate package directory containing the colcon command-prefix script. Source manifests alone do not prove that an arbitrary `build` directory is generated. |
| ROS 安装产物 / ROS install | 已识别工作空间，且 `install` 直接包含 `setup.bash`、`local_setup.bash`、`_local_setup_util_sh.py`。A recognized workspace and all three colcon environment scripts directly in `install`. |
| ROS 构建日志 / ROS build logs | 工作空间的 `build` 有 colcon 标记，同时存在 `log`。A colcon-marked `build` and a sibling `log` directory. |
| ROS 原始录制 / ROS recordings | 同一目录直接包含 `metadata.yaml` 和 `.db3` / `.mcap` 普通文件。A directory containing both a metadata file and a DB3 or MCAP regular file. |
| 点云 / 大数据 / Point clouds and large data | `.pcd`、`.bag`、`.db3`、`.mcap`、`.ply`、`.las`、`.laz`，或逻辑大小至少 256 MiB 的普通文件。按直接所在目录聚合匹配文件。Matching data extensions or regular files at least 256 MiB; matching files are aggregated only in their immediate directory. |
| pip 缓存 / pip cache | 当前用户缓存根的 `pip` 下含 `http-v2`、`http`、`wheels` 或 `selfcheck` 子目录。The standard user cache location with a recognized pip cache subdirectory. |
| npm 缓存 / npm cache | `~/.npm/_cacache` 下含 `content-v2` 或 `index-v5`。The standard content-addressed npm cache structure. |
| ccache 缓存 / ccache | 用户缓存根的 `ccache` 或 `~/.ccache`，含 `ccache.conf`，或十六进制分片目录内的 `stats`。A standard location with a configuration file or hexadecimal shard statistics. |
| Conda 环境 / Conda environment | 子目录 `conda-meta` 内有 `history` 或 `.json` 普通文件。A `conda-meta` directory containing history or JSON package records. |

工作空间证据来自 `src` 子树内包含 `<package` 的 `package.xml` 或 colcon 构建标记。路径和结构匹配不保证每个文件都由对应工具生成。用户自定义 pip/npm/ccache 缓存位置不在当前规则内；绝对路径的 `XDG_CACHE_HOME` 会用于标准缓存根。嵌套分组单列，并从上层分组扣除，避免表格相加时重复统计。

Workspace evidence comes from a source `package.xml` containing `<package`, or from colcon build markers. A matching path or structure does not prove the origin of every contained file. Custom pip/npm/ccache locations are outside the current rules; an absolute `XDG_CACHE_HOME` supplies the standard cache root. Nested groups are reported separately and deducted from parent groups to avoid double counting.

rosbag、点云和数据组可能是唯一原始采集副本，应先备份或迁移并验证可读性。Conda 环境是正在使用的依赖环境；历史构建日志也不能通过重新编译还原。pip/npm 缓存通常可以重新获取，但离线环境、私有软件源或已下架依赖可能阻止恢复。1.2.0 不查询或清理 Docker 镜像、容器和卷。

Recordings and point-cloud/data groups may be the only original copies; back up or migrate them and verify the copy first. Conda directories are usable dependency environments, and historical build logs cannot be reproduced by rebuilding. pip/npm caches can often be fetched again, but offline use, private sources, and removed dependencies can prevent recovery. Version 1.2.0 does not query or clean Docker images, containers, or volumes.

## 6. 软件空间画像 / Application Storage Profile

在「已安装软件」中选择应用，点击「空间画像」。对话框分别列出软件包记录体积、已知关联目录的实际占用与逻辑大小，并解释关联依据。它没有把软件本体、缓存和用户数据合并成“总可清理体积”；同一应用的多个安装范围也可能共用用户数据。

Select an application in **Installed Software**, then open **Storage Profile**. The dialog separates package-manager size estimates from allocated/logical measurements of known associated directories and explains the association. It does not combine package files, cache, and user data into a “total reclaimable size”; multiple installations can share user data.

`$CACHE` 表示绝对 `XDG_CACHE_HOME` 或默认 `~/.cache`；`$CONFIG` 表示绝对 `XDG_CONFIG_HOME` 或默认 `~/.config`。只列出现有真实目录；未发现支持的目录不表示该应用没有用户数据。DEB 架构后缀（如 `:amd64`）在匹配包名时移除。

`$CACHE` means absolute `XDG_CACHE_HOME`, defaulting to `~/.cache`; `$CONFIG` means absolute `XDG_CONFIG_HOME`, defaulting to `~/.config`. Only existing real directories are listed. No supported directory found does not mean no user data exists. DEB architecture suffixes such as `:amd64` are removed for package-name matching.

| 来源或 DEB 包名 / Source or DEB package | 当前关联目录 / Current associations |
| --- | --- |
| Snap | `~/snap/<snap-name>`；包括版本目录与 `common`，不跟随 `current` 软链接。Revision directories and `common`; the `current` symlink is not followed. |
| Flatpak | `~/.var/app/<application-id>`；可同时包含缓存、配置和用户数据。May contain cache, configuration, and user data together. |
| `google-chrome-stable` | `$CACHE/google-chrome`、`$CONFIG/google-chrome` |
| `google-chrome-beta` | `$CACHE/google-chrome-beta`、`$CONFIG/google-chrome-beta` |
| `google-chrome-unstable` | `$CACHE/google-chrome-unstable`、`$CONFIG/google-chrome-unstable` |
| `chromium` / `chromium-browser` | `$CACHE/chromium`、`$CONFIG/chromium` |
| `code` | `$CACHE/Code`、`$CONFIG/Code`、`~/.vscode/extensions` |
| `code-insiders` | `$CACHE/Code - Insiders`、`$CONFIG/Code - Insiders`、`~/.vscode-insiders/extensions` |
| `vlc` | `$CACHE/vlc`、`$CONFIG/vlc` |
| `gimp` | `$CACHE/GIMP`、`$CONFIG/GIMP` |
| `firefox` | `$CACHE/mozilla/firefox`、`~/.mozilla/firefox` |
| `thunderbird` | `$CACHE/thunderbird`、`$CONFIG/thunderbird`、`~/.thunderbird` |

Snap / Flatpak 使用明确命名空间；DEB 使用上述小型内置目录规则，没有模糊包名匹配。关联不是完整覆盖承诺，也不是清理授权。配置、浏览器档案、扩展、邮件和容器数据均可能需要保留。各目录单独统计，重叠路径或共享块使其不适合直接相加。

Snap and Flatpak use explicit namespaces. DEB uses the small catalog above, without fuzzy package-name matching. Associations do not promise complete coverage or authorize cleaning. Configuration, browser profiles, extensions, mail, and container data may need to be retained. Locations are measured independently; overlapping paths or shared blocks prevent a reliable simple sum.

## 7. 保存和阅读报告 / Save and interpret reports

CSV 使用 UTF-8 BOM、引号与双引号转义；可能被电子表格解释为公式的 `= + - @` 前缀（含前导空白情形）、前导制表符或回车会添加单引号。写入通过 `QSaveFile` 提交。导出不会启动新的内容扫描。

CSV uses a UTF-8 BOM, quoted cells, and escaped double quotes. Potential spreadsheet formula prefixes (`= + - @`, including after leading whitespace), leading tabs, and carriage returns are prefixed with an apostrophe. `QSaveFile` commits the output. Export does not trigger a new content scan.

- **目录页**：导出当前目录及其全部已扫描后代，搜索不改变范围；目录行含子树总量，不应把所有层级行再次相加。
- **重复页**：导出全部已完成分组，搜索不隐藏组成员；报告记录实际扫描阈值、扫描/完整校验文件数及逻辑冗余口径。每个文件行重复列出所属组冗余量，该列应按组读取，不按文件行累加。
- **开发环境**：导出全部已显示分组，搜索和类别筛选不改变范围；记录扫描根、完成状态、跳过项、省略组数以及统计总计。达到 2,000 组显示上限时，CSV 不是全部已识别组的明细。

- **Directory tab**: exports the current directory and all scanned descendants. Search does not narrow the export. Directory rows contain subtree totals; summing every hierarchy level would double count.
- **Duplicate tab**: exports all completed groups and all their members, independent of search. Metadata records the actual scan threshold, inspected/full-hashed counts, and logical-redundancy semantics. Group redundancy repeats on each member row; read it once per group.
- **Developer Environment**: exports all displayed groups independent of search/category filters, with the scan root, completion state, omissions, and totals. At the 2,000-group display limit, the CSV is not a complete list of every identified group.

导出会拒绝覆盖扫描根内已存在的输入文件或目录；请使用新报告名，或将报告保存在扫描根以外。报告中的取消、数量/深度上限、跳过与错误字段应与数值一起阅读；结果是快照，不是持续更新的磁盘账本。

Export refuses to overwrite existing inputs inside the scanned root. Choose a new report name or save outside that root. Read cancellation, count/depth limits, omissions, and errors together with the numbers. Reports are snapshots rather than continuously updated storage ledgers.

## 源码与验证入口 / Source and verification map

| 行为 / Behavior | 实现 / Implementation | 验证 / Verification |
| --- | --- | --- |
| 目录统计、重复哈希与边界 / Tree statistics, duplicate hashing, boundaries | [`spaceanalyzer.h`](../src/spaceanalyzer.h)、[`spaceanalyzer.cpp`](../src/spaceanalyzer.cpp) | [`spaceanalyzer_test.cpp`](../tests/spaceanalyzer_test.cpp) |
| 层级导航、比例图、重复页、CSV / Navigation, chart, duplicates UI, CSV | [`explorerpage.cpp`](../src/explorerpage.cpp) | [`explorerpage_test.cpp`](../tests/explorerpage_test.cpp) |
| 开发分类、去重与报告 / Developer classification, accounting, reports | [`developerbackend.cpp`](../src/developerbackend.cpp)、[`developerpage.cpp`](../src/developerpage.cpp) | [`developer_test.cpp`](../tests/developer_test.cpp)、[`developerpage_test.cpp`](../tests/developerpage_test.cpp) |
| 软件目录关联与统计 / Application associations and measurement | [`appstorage.cpp`](../src/appstorage.cpp)、[`appstoragedialog.cpp`](../src/appstoragedialog.cpp) | [`appstorage_test.cpp`](../tests/appstorage_test.cpp)、[`appstoragedialog_test.cpp`](../tests/appstoragedialog_test.cpp) |

自动化测试覆盖临时夹具、路径边界、统计和生产样式下的界面操作。真实大目录、网络文件系统、不同 Ubuntu / ARM 桌面环境中的耗时与体验仍应单独验证。

Automated tests cover temporary fixtures, path boundaries, accounting, and UI behavior under the production style. Performance and experience on real large trees, network filesystems, and different Ubuntu/ARM desktop environments require separate validation.
