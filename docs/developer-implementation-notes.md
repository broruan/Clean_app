# 开发环境占用模块 / Developer storage inventory

`DeveloperPage` 仅在 `ensureScanStarted()` 或 `rescan()` 时启动异步扫描。`setRoot()` 可以在空闲时更换目录并重置结果；不自动扫描新路径。析构和 `cancelWork()` 都会停止扫描，析构等待工作线程退出。页面只提供打开目录、复制路径和导出 CSV，不运行构建、缓存清理、Conda 或 Docker 命令。

## 结构识别

| 分类 | 必需证据 |
| --- | --- |
| ROS / colcon 构建目录 | `build/.built_by` 内容为 `colcon`，或 `build/<包>/colcon_command_prefix_build.sh`；只有 `src/package.xml` 不足以将同名 `build` 认定为可重建产物 |
| ROS 安装目录 | 已识别工作空间，且 `install` 直接包含 `setup.bash`、`local_setup.bash`、`_local_setup_util_sh.py` |
| ROS 构建日志 | 已识别工作空间的 `build` 有上述 colcon 标记，同时存在 `log` 目录 |
| ROS 原始录制 | 同一目录直接包含 `metadata.yaml` 和 `.db3` / `.mcap` 普通文件 |
| 点云 / 大数据 | `.pcd`、`.bag`、`.db3`、`.mcap`、`.ply`、`.las`、`.laz`，或逻辑大小至少 256 MiB；仅按直接所在目录聚合匹配文件 |
| pip 缓存 | 当前用户缓存根的 `pip` 中含 `http-v2`、`http`、`wheels` 或 `selfcheck` 子目录 |
| npm 缓存 | 当前用户 `~/.npm/_cacache` 中含 `content-v2` 或 `index-v5` 子目录 |
| ccache | 当前用户缓存根的 `ccache` 或 `~/.ccache`；包含 `ccache.conf`，或十六进制分片目录内的 `stats` 文件 |
| Conda 依赖环境 | 子目录 `conda-meta` 内有 `history` 或 `.json` 普通文件 |

`XDG_CACHE_HOME` 仅在绝对路径时生效；用户更改 pip/npm/ccache 的其他自定义缓存位置不在本轮规则内。Conda 环境不作为缓存；历史日志不可重建；rosbag 和数据组明确要求保留或先备份。结构识别用于解释目录用途，不构成删除建议或可恢复性保证。

## 统计和边界

后端对选定目录进行一次基于 Linux 文件描述符的深度优先遍历，再用后序累加构建目录统计。默认最多检查 500,000 项、50,000 个目录、32 层；触及上限或取消后展示部分结果并提示。默认显示和导出占用最大的 2,000 组，识别总计仍包含被省略组，页面会显示省略说明。

统计只包含已扫描普通文件的 `st_blocks * 512`，逻辑大小另列；不含目录元数据。硬链接按设备和 inode 仅计首次扫描的一份；嵌套分组单独列出，并从上层分组扣除。物理共享的 reflink 数据块不作共享去重。未识别的目录仍计入已扫描总量，不计入已识别总量。

根路径逐段 `openat(..., O_NOFOLLOW)` 打开；子目录在打开后再次验证设备与 inode；不跟随符号链接。设备号、`statx` 挂载 ID、`/proc/self/mountinfo` 共同限制挂载边界，包括同设备 bind mount。标记文件通过目录描述符以 no-follow、nonblocking 方式限量读取并校验身份。没有权限、特殊文件和挂载边界会被跳过，不提权。

CSV 通过 `QSaveFile` 原子写入，使用 UTF-8 BOM、引号转义，并对可能的电子表格公式前缀添加单引号。导出全部已显示分组，搜索和类别筛选不改变导出范围；文件开头包含扫描根目录、范围、部分状态、跳过与省略项、扫描和识别总计，每行含扫描完成或部分状态。省略分组时标记部分结果。拒绝覆盖扫描根内已经存在的文件或目录；扫描根内的新报告使用同目录私有暂存文件，再通过不会覆盖现有路径的 `link(2)` 原子发布，避免存在性检查和提交之间的竞争。可选择新的报告文件名，或在扫描根之外覆盖旧报告。

## 验证范围

`developer_test` 覆盖结构证据与普通同名目录区分、XDG 缓存位置、Conda 环境说明、原始数据保护、嵌套去重、硬链接、软链接和根路径拒绝、取消、扫描上限、导出引号与公式转义。

`developerpage_test` 使用生产 Fusion / QSS 与中文字体，覆盖延迟扫描、搜索和分类、全行选择和详情、复制路径、筛选后完整导出、选中数值深色可读性、取消、切换根目录和扫描中析构。
