# Branding and icon / 品牌与图标

<p align="center"><img src="../assets/disklens.svg" width="160" height="160" alt="DiskLens disk and lens icon"></p>

## English

The project name is **DiskLens**, with the Chinese name **磁盘透镜**. The original disk-and-lens icon is maintained as editable vector source in [`assets/disklens.svg`](../assets/disklens.svg). The same asset appears in the README, application window, and installed desktop launcher, so documentation and the shipped application share one visual identity.

SVG keeps the icon scalable for documentation and desktop launchers. It is embedded through [`assets/resources.qrc`](../assets/resources.qrc) and installed to the scalable `hicolor` icon theme by CMake. Preserve the square view box, transparent exterior, and legibility at small sizes when editing it.

The original icon is covered by this repository's MIT license. Qt is an independent library with separate licensing; use of Qt does not imply Qt endorsement. See [third-party notices](../THIRD_PARTY_NOTICES.md).

The screenshots in `docs/images/` show the actual Chinese interface. Example scan files are demonstration data; installed software inventory is obtained from the capture environment. Screenshots should be regenerated from the application after visible UI changes and reviewed for private paths or metadata.

## 简体中文

项目英文名为 **DiskLens**，中文名为 **磁盘透镜**。原创“磁盘 + 透镜”图标以可编辑矢量源码保存在 [`assets/disklens.svg`](../assets/disklens.svg)。README、应用窗口与安装后的桌面启动器使用同一份图标。

SVG 可无损缩放，适合文档和不同大小的桌面入口。图标通过 [`assets/resources.qrc`](../assets/resources.qrc) 嵌入程序，并由 CMake 安装到 `hicolor` 的 scalable 图标目录。修改时应保留方形 view box、外部透明区域与小尺寸辨识度。

原创图标使用本仓库 MIT 许可证。Qt 是独立许可的第三方库，使用 Qt 不表示获得 Qt 官方背书，详见 [第三方声明](../THIRD_PARTY_NOTICES.md)。

`docs/images/` 截图来自真实中文界面；扫描目录使用示例数据，软件库存来自截图环境。界面发生可见变化后应重新截图，并检查是否暴露个人路径或元数据。
