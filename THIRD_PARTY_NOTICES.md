# Third-party notices / 第三方声明

## English

DiskLens source code and its original disk-and-lens SVG icon are licensed under [MIT](LICENSE). This license does not change the licenses of libraries, tools, fonts, or artwork supplied by the operating system.

### Qt

DiskLens dynamically links Qt Core, Widgets, and Concurrent. Automated tests additionally use Qt Test. SVG rendering uses the distribution's Qt SVG library / plugin. These components are installed as Ubuntu packages rather than bundled library copies in the DiskLens `.deb`.

Qt is developed by The Qt Company and its contributors. Qt is offered under open-source and commercial terms that depend on the version and module. The Ubuntu Qt 5 base package's copyright file lists **LGPL-3 or GPL-2** for its primary library files, with additional licenses for particular files and embedded components. Consult the exact packages you install for authoritative copyright and license notices, for example:

```bash
dpkg-query -W 'libqt5core*' 'libqt5widgets*' 'libqt5concurrent*' 'libqt5svg*'
less /usr/share/doc/libqt5core5a/copyright
```

Library package names can differ by Ubuntu version, including `t64` suffixes. Use the relevant package's file under `/usr/share/doc/` when a name differs. LGPL and GPL license texts are available on Ubuntu in `/usr/share/common-licenses/`. A source build using Qt 6 must follow the licenses of that selected Qt 6 distribution. The MIT license on DiskLens does not grant a commercial Qt license.

### System integrations

DiskLens invokes system-installed `dpkg-query`, APT, optional Snap / Flatpak tools, `pkexec`, and desktop folder-opening integration. These tools, the C / C++ runtime, desktop icon themes, and fonts retain their own copyright and licenses. DiskLens does not relicense them. The package recommends Noto CJK fonts; fonts are supplied separately by Ubuntu.

Installed application names and icons can appear in the live interface and screenshots through the system's desktop metadata and icon themes. Their inclusion identifies those applications and does not imply authorship, affiliation, or endorsement by their owners.

If redistributing a modified binary or bundling dependencies yourself, preserve the project's MIT notice and comply with the actual third-party components you distribute, including their source and notice requirements where applicable.

## 简体中文

DiskLens 源码与原创“磁盘 + 透镜”SVG 图标使用 [MIT 许可证](LICENSE)。该许可证不会改变操作系统所提供的库、工具、字体和图形资源的授权。

### Qt

程序动态链接 Qt Core、Widgets、Concurrent，自动测试还使用 Qt Test。SVG 图标由发行版的 Qt SVG 库 / 插件渲染。这些组件通过 Ubuntu 包安装，DiskLens 的 `.deb` 不捆绑 Qt 库副本。

Qt 由 The Qt Company 与贡献者开发，按版本与模块提供不同的开源或商业授权。Ubuntu Qt 5 base 包的版权文件对主要库文件列出 **LGPL-3 或 GPL-2**，个别文件和内嵌组件还可能采用其他许可证。准确条款应以实际安装包的版权与许可证文件为准，可参考上方命令。

不同 Ubuntu 版本的库包名可能包含 `t64` 等后缀，应查看 `/usr/share/doc/` 下对应包的版权文件。Ubuntu 的 `/usr/share/common-licenses/` 提供 LGPL / GPL 许可证文本。使用 Qt 6 源码构建时，应遵守所选 Qt 6 发行包的许可。DiskLens 的 MIT 许可证不会授予 Qt 商业许可。

### 系统集成

程序调用系统安装的 `dpkg-query`、APT、可选 Snap / Flatpak 工具、`pkexec` 和桌面文件夹打开接口。这些工具、C / C++ 运行库、桌面图标主题和字体保留各自版权与许可证。安装包推荐的 Noto CJK 字体由 Ubuntu 单独提供。

实时界面与截图可能根据系统桌面元数据和图标主题展示已安装应用的名称与图标，仅用于识别应用，不代表 DiskLens 拥有其版权、与其有关联或得到其背书。

重新分发修改后的二进制或自行捆绑依赖时，请保留本项目 MIT 声明，并遵守实际分发组件的源码、声明及其他许可要求。
