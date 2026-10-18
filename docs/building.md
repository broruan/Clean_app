# Building and packaging / 构建与打包

[English README](../README.md) · [中文 README](../README.zh-CN.md)

## English

### Requirements

Release packages use Qt 5, C++17, and CMake 3.16 or later. Build on the Ubuntu release and architecture you intend to package: a successful build on one release does not establish binary compatibility with another.

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config qtbase5-dev libqt5svg5 qttranslations5-l10n dpkg-dev \
  desktop-file-utils libxml2-utils pkexec xdg-utils fonts-noto-cjk baobab
```

Qt SVG support supplies the application icon renderer. `pkexec` provides privileged package-manager authorization; a graphical Polkit agent must be available in your desktop session. `xdg-utils` provides the system folder-opening integration. `fonts-noto-cjk` supplies Chinese glyphs; `qttranslations5-l10n` provides translations for Qt standard dialogs. Snap and Flatpak tools are optional and only needed for their respective installed-software sources.

### Configure, build, test

```bash
git clone https://github.com/broruan/DiskLens.git
cd DiskLens
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDISKLENS_QT_MAJOR=5
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/disklens
```

`DISKLENS_QT_MAJOR` selects Qt 5 or Qt 6. Release CI uses Qt 5. Choosing Qt 6 is a source-build option, not a claim that Qt 6 packages were validated in the release matrix; install the corresponding Qt 6 Widgets / Concurrent / Test development packages, SVG plugin and `qt6-translations-l10n` first.

`baobab` supplies a harmless installed desktop application for inventory mapping and read-only APT preview tests; CI installs it for those checks. It is a test fixture, not a DiskLens runtime dependency.

CTest contains thirteen targets: `scanner`, `inventory`, `uninstall`, `cache`, `cachepage`, `spaceanalyzer`, `explorerpage`, `developer`, `developerpage`, `appstorage`, `appstoragedialog`, `appearance`, and `localization`. Conditional integration cases may be skipped when an optional package manager or fixture application is absent. Read the test output as well as its final status. See [validation scope](validation.md).

### Translation catalogs

UI text uses the `dlTr()` wrapper and the `DiskLens` Qt translation context. The English catalog is [`translations/disklens_en.ts`](../translations/disklens_en.ts); its compiled `.qm` is committed and embedded in the application. Normal builds do not require `lrelease` or a translation toolchain.

After changing translatable source strings or English translations, install the developer-only tool and rebuild the catalog:

```bash
sudo apt install qttools5-dev-tools
python3 tools/update-translations.py --compile
```

Commit both `.ts` and `.qm` changes. Preserve placeholders, markup and intentional whitespace, then run the localization test and inspect both languages with the production stylesheet.

### Read-only UI smoke checks

Use a small directory you are allowed to scan. Screenshots may contain local paths and installed software names; review them before sharing.

```bash
QT_QPA_PLATFORM=offscreen ./build/disklens --smoke-test \
  --language en --font-size 13 \
  --scan-path /tmp --page overview --screenshot /tmp/disklens-overview-en.png
QT_QPA_PLATFORM=offscreen ./build/disklens --smoke-test \
  --language zh_CN --font-size 17 \
  --scan-path /tmp --page cache --screenshot /tmp/disklens-cache-zh-large.png
```

`--page` accepts `overview`, `apps`, `files`, `cache`, `explorer`, `duplicates`, or `developer`. Smoke mode waits for inventory and scan work, checks UI state, optionally saves a screenshot, and exits. It does not execute uninstall, authorization, or cache deletion. The explicit `duplicates` page reads candidate file contents for verification; the developer page reads small classification marker files. Offscreen checks do not replace testing an actual desktop session.

### Build a Debian package

```bash
bash packaging/build-deb.sh --qt-major 5 --jobs 2
```

The script builds in a fresh temporary directory, runs tests, creates and validates the package, and writes it under `dist/`. It does not install the package. For a different output directory:

```bash
bash packaging/build-deb.sh --qt-major 5 --jobs 2 --output-dir /tmp/disklens-packages
```

Names include version, Ubuntu release, and Debian architecture, for example `disklens_1.3.0_ubuntu24.04_arm64.deb`. The script discovers shared-library dependencies with `dpkg-shlibdeps` and adds runtime integrations such as the matching SVG plugin, `pkexec`, and `xdg-utils`. This allows the package metadata to follow each Ubuntu release's actual library names, including `t64` transitions.

### Release automation

[`.github/workflows/build.yml`](../.github/workflows/build.yml) defines native Ubuntu 22.04 / 24.04 / 26.04 jobs for `amd64` and `arm64`. A version tag such as `v1.3.0` triggers the release workflow. All matrix jobs must succeed before the release job gathers six packages and publishes them with `SHA256SUMS`. Check the workflow attached to the tag before stating that a platform passed.

## 简体中文

### 环境与依赖

发布包使用 Qt 5、C++17 与 CMake 3.16 或更新版本。应在目标 Ubuntu 版本与架构上构建，不应把某个版本的构建成功视为其他系统版本的二进制兼容验证。

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config qtbase5-dev libqt5svg5 qttranslations5-l10n dpkg-dev \
  desktop-file-utils libxml2-utils pkexec xdg-utils fonts-noto-cjk baobab
```

Qt SVG 支持用于渲染应用图标；`pkexec` 调用系统授权执行软件包管理操作，桌面会话还需要可用的图形 Polkit 授权代理；`xdg-utils` 用于打开文件夹；`fonts-noto-cjk` 提供中文字体；`qttranslations5-l10n` 提供 Qt 标准对话框译文。Snap 与 Flatpak 命令为可选组件，仅各自来源的软件管理需要。

### 编译与测试

```bash
git clone https://github.com/broruan/DiskLens.git
cd DiskLens
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDISKLENS_QT_MAJOR=5
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/disklens
```

`DISKLENS_QT_MAJOR` 可选择 Qt 5 或 Qt 6，发布 CI 固定使用 Qt 5。选择 Qt 6 是源码构建选项，不代表发布矩阵已验证 Qt 6；请先安装对应的 Widgets / Concurrent / Test 开发包、SVG 插件与 `qt6-translations-l10n`。

`baobab` 为桌面入口映射与只读 APT 预览测试提供无害的已安装应用，CI 会安装它；这是测试样例，不是 DiskLens 运行依赖。

CTest 包含 `scanner`、`inventory`、`uninstall`、`cache`、`cachepage`、`spaceanalyzer`、`explorerpage`、`developer`、`developerpage`、`appstorage`、`appstoragedialog`，以及新增 `appearance` 与 `localization`，共十三个目标。缺少可选包管理器或测试所需应用时，部分集成测试会跳过，应同时阅读输出中的跳过说明。详见 [验证范围](validation.md)。

### 翻译资源

界面文本使用 `dlTr()` 包装与 `DiskLens` Qt 翻译上下文。英文目录为 [`translations/disklens_en.ts`](../translations/disklens_en.ts)，对应 `.qm` 已提交并嵌入程序，普通源码构建不需要 `lrelease` 或翻译工具链。

修改待翻译字符串或英文译文后，安装仅供开发使用的 `qttools5-dev-tools`，运行 `python3 tools/update-translations.py --compile`，同时提交 `.ts` 与 `.qm`。保留占位符、标记及有意使用的空白，运行本地化测试，并使用正式样式检查两种语言。

### 只读界面检查

选择较小且有权读取的目录，使用上方英文部分的两条 `--smoke-test` 命令。截图可能包含本机路径和软件名称，分享前请检查。

`--page` 支持 `overview`、`apps`、`files`、`cache`、`explorer`、`duplicates`、`developer`。检查模式等待库存读取与扫描，验证界面状态、按需截图后退出，不执行卸载、系统授权或缓存删除。指定 `duplicates` 会读取候选文件内容进行校验，开发目录识别会限量读取分类标记文件。离屏检查不能代替真实桌面会话验证。

### 生成安装包

```bash
bash packaging/build-deb.sh --qt-major 5 --jobs 2
```

脚本在全新临时目录构建、运行测试、生成并检查安装包，默认输出至 `dist/`，不会安装软件。通过 `--output-dir /tmp/disklens-packages` 可指定其他目录。

包名包含版本、Ubuntu 版本与 Debian 架构，例如 `disklens_1.3.0_ubuntu24.04_arm64.deb`。依赖通过 `dpkg-shlibdeps` 根据实际链接库生成，并补充对应 SVG 插件、`pkexec` 与 `xdg-utils`，可跟随不同 Ubuntu 版本的库命名及 `t64` 变化。

### 发布流水线

[`.github/workflows/build.yml`](../.github/workflows/build.yml) 定义 Ubuntu 22.04 / 24.04 / 26.04 的 `amd64`、`arm64` 原生构建任务。推送 `v1.3.0` 形式的版本标签后触发发布；全部矩阵任务成功后，发布任务汇总六个安装包并附带 `SHA256SUMS`。只有对应标签的实际工作流结果才能证明某个平台已经通过。
