# Architecture / 源码结构

DiskLens is a single local desktop application. There is no backend service or cloud account. Filesystem scanning and cache cleanup use Linux interfaces; software inventory and removal use installed system package managers.

DiskLens 是单进程本机桌面应用，没有后台服务或云账户。文件扫描与缓存清理使用 Linux 接口，软件库存和卸载调用本机已有的软件包管理器。

```mermaid
flowchart TD
    UI[Qt Widgets interface / 界面]
    Main[MainWindow / 页面与任务调度]
    CachePage[CachePage / 选择与清理确认]
    Inventory[Package inventory / 软件库存]
    Scanner[Large-file scanner / 大文件扫描]
    Cache[Cache backend / 缓存后端]
    Uninstall[Uninstall controller / 卸载状态机]
    FS[Linux filesystem / 文件系统]
    PM[dpkg / APT / Snap / Flatpak]
    Auth[System Polkit dialog / 系统授权]
    UI --> Main
    Main --> CachePage
    Main --> Inventory
    Main --> Scanner
    Main --> Uninstall
    CachePage --> Cache
    Scanner --> FS
    Cache --> FS
    Inventory --> PM
    Uninstall --> PM
    Uninstall --> Auth
```

| Source | Responsibility / 职责 |
| --- | --- |
| [`src/main.cpp`](../src/main.cpp) | Application style, command-line options, screenshot / read-only smoke entry point. 应用样式、命令行、截图与只读检查入口。 |
| [`src/mainwindow.cpp`](../src/mainwindow.cpp) | Page navigation, background inventory / scan work, volume usage, uninstall confirmation and logs. 页面导航、库存与扫描任务、磁盘用量、卸载确认与日志。 |
| [`src/models.cpp`](../src/models.cpp) | Inventory and file table models, filtering, sorting, and delegates. 软件与文件模型、筛选、排序和绘制。 |
| [`src/widgets.cpp`](../src/widgets.cpp) | Shared presentation, disk chart, and production stylesheet. 共享控件、磁盘图表与正式样式表。 |
| [`src/packagebackend.cpp`](../src/packagebackend.cpp) | Read-only package queries, desktop-entry mapping, size accounting, initial protection metadata. 只读包查询、桌面入口映射、体积口径和初始保护信息。 |
| [`src/filescanner.cpp`](../src/filescanner.cpp) | Bounded top-N scan, link handling, mount boundaries, cancellation. Top-N 扫描、链接与挂载边界、取消。 |
| [`src/cachepage.cpp`](../src/cachepage.cpp) | Cache table, full-row / Space selection, filtered selection, confirmation, background cleanup and rescan. 缓存表格、整行与空格勾选、筛选选择、确认、后台清理和重扫。 |
| [`src/cachebackend.cpp`](../src/cachebackend.cpp) | Cache / log grouping, signed in-process scan manifests, path and file revalidation, selected-file unlinking. 缓存日志分组、进程内快照签名、路径与文件复核、删除所选文件。 |
| [`src/uninstallcontroller.cpp`](../src/uninstallcontroller.cpp) | Asynchronous preview / revalidation / removal stages, process logs, identifier checks, package protection. 异步预览、复核与卸载状态机、日志、标识校验与组件保护。 |
| [`tests/`](../tests) | Backend fixtures, installed-system read-only checks, and styled cache-page regression tests. 后端临时目录测试、已安装系统只读检查与正式样式界面回归。 |
| [`packaging/`](../packaging) | Desktop entry, isolated package build, dependency generation and package checks. 桌面入口、隔离构建、依赖生成和包检查。 |

`QtConcurrent` runs filesystem work and inventory reads outside the UI thread. Cancellation is cooperative. `QProcess` runs external commands with separate argument entries, without evaluating user input in a shell. Removing software is a separate state machine: a successful preview alone does not execute a removal, and execution revalidates the preview.

`QtConcurrent` 将文件扫描与库存读取移出界面线程，取消由任务协作完成。`QProcess` 以独立参数调用外部命令，不将用户文本交给 shell 求值。软件卸载使用独立状态机：完成预览不会自动执行，执行前还会再次校验。

See the [user guide](usage.md) for behavioral limits and [validation scope](validation.md) for evidence boundaries. 行为限制见 [使用指南](usage.md)，验证边界见 [验证范围](validation.md)。
