# 更新日志 / Changelog

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
