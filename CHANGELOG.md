# 更新日志 / Changelog

## Unreleased

- 中文：语言切换改为主界面独立按钮，中文界面显示 `English`，英文界面显示 `中文`；English: Language switching is now a standalone main-window button, showing `English` in Chinese mode and `中文` in English mode.
- 中文：比较模式下点击行内任意位置即可切换该文件的复选框，并选中该行；English: In compare mode, clicking anywhere in a row toggles its checkbox and selects the row.

### 中文

- Windows 和 Linux GUI 新增 Arctic Blue、Midnight Cyan、Warm Orange、Jade Mist、Violet Cloud 与 Graphite Amber 六套主题。
- 新增主题即时切换与本地记忆，重新启动后恢复上次使用的主题。
- 将普通拖放提示升级为带文件图标、主题色边框和独立背景的拖放卡片。
- 统一主按钮、危险操作、禁用状态、进度条、选中行和结果表格的语义化配色。
- 加深 Arctic Blue 的冰蓝色层级，避免默认主题看起来接近白色界面。

### English

- Adds six themes to the Windows and Linux GUIs: Arctic Blue, Midnight Cyan, Warm Orange, Jade Mist, Violet Cloud, and Graphite Amber.
- Adds instant theme switching and local persistence across launches.
- Replaces the plain drag hint with a file-icon drop-zone card using the active theme's border and surface colors.
- Unifies semantic colors for the primary action, destructive action, disabled state, progress bars, selected rows, and results table.
- Deepens the Arctic Blue layers so the default theme no longer looks like a near-white interface.

## 1.1.0 — 2026-09-15

### 中文

- 新增 Compare / Confirm 流程，可选择两条记录并根据已计算的校验值判断文件是否一致。
- 比较只依据校验结果，不将文件路径、大小或修改时间作为内容一致性的判断依据。
- 增加比较结果弹窗、取消比较操作和 Esc 快捷键；界面默认中文并支持切换 English。

### English

- Adds a Compare / Confirm flow for selecting two records and checking whether their calculated hash values match.
- Comparison uses hash results only; file paths, sizes, and modified times are not used as content-equality criteria.
- Adds a comparison result dialog, comparison cancellation, and the Esc shortcut; Chinese is the default UI language with English available.

## 1.0.0 — 2026-09-10

### 中文

- 发布首个公开版本。
- 支持 CRC32、MD5、SHA-1、SHA-256、SHA-384 和 SHA-512。
- 支持单文件一次读取计算多个算法，以及多个文件并发计算。
- Windows 和 Linux 原生 GUI 支持拖拽、自动开始、双层进度、删除、清空、右键菜单和结果复制。
- 默认选中 MD5 与 SHA-256；复制结果包含绝对路径、文件名、文件大小、修改时间、状态和各算法结果。
- 添加跨平台核心测试、性能测试入口、Windows x86 构建脚本和 GitHub Actions CI。

### English

- First public release.
- Supports CRC32, MD5, SHA-1, SHA-256, SHA-384, and SHA-512.
- Calculates multiple algorithms in one pass per file and processes multiple files concurrently.
- Native Windows and Linux GUIs support drag-and-drop, automatic start, two-level progress, deletion, clearing, context menus, and result copying.
- MD5 and SHA-256 are selected by default; copied results include absolute path, file name, size, modified time, status, and every algorithm result.
- Adds cross-platform core tests, a benchmark entry point, a Windows x86 build script, and GitHub Actions CI.
