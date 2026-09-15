# Lizy File Hash Tool v1.1

轻量、快速的跨平台文件校验工具。核心使用 C++17 实现，同一文件只读取一次即可同时计算多个算法；多个文件使用独立 worker 并发处理。

A lightweight and fast cross-platform file verification tool. The shared core is written in C++17: each file is read once while multiple algorithms are calculated, and different files are processed concurrently by independent workers.

## 功能特性 / Features

- 支持 CRC32、MD5、SHA-1、SHA-256、SHA-384、SHA-512；默认选中 MD5 和 SHA-256。
- Supports CRC32, MD5, SHA-1, SHA-256, SHA-384, and SHA-512; MD5 and SHA-256 are selected by default.
- 支持拖拽或添加文件后自动开始，无需 Start 按钮；计算期间可以继续添加或删除文件。
- Dragging or adding files starts hashing automatically. Files can be added or removed while other files are running.
- 每个文件有独立进度条，同时显示所有文件的总进度；文件之间互不阻塞。
- Each file has an independent progress bar, plus an overall progress bar for all files.
- 支持 Delete 键、右键菜单、Clean all、复制单条结果和复制全部结果。
- Supports the Delete key, a context menu, Clean all, copying one result, and copying all results.
- 点击 Compare 后可勾选两条记录，点击 Confirm 自动比较已计算的 MD5、SHA 和其他校验结果。
- Click Compare to select two records, then click Confirm to compare their calculated MD5, SHA, and other hash values.
- 默认使用中文界面，可通过语言选项切换到 English；确认比较后会弹窗显示一致或不一致，也可取消比较。
- Chinese is the default UI language; the language option switches to English. Confirmation shows a comparison dialog, and an active comparison can be cancelled.
- 复制结果包含每个文件的绝对路径、文件名、文件大小、修改时间、状态及各算法结果，每个属性独立一行。
- Copied results include the absolute path and file name, size, modified time, status, and each algorithm result, with one attribute per line.
- 无文件时显示“文件拖拽到此处”提示，并使用蓝色钥匙主题图标。
- An empty list displays “文件拖拽到此处” and the application uses a blue key-themed icon.
- Windows 与 Linux GUI 提供醒目的文件拖放卡片和六套可即时切换的主题，主题选择会在下次启动时自动恢复。
- The Windows and Linux GUIs provide a prominent file drop-zone card and six instantly switchable themes; the selected theme is restored on the next launch.

## 界面主题 / UI themes

GUI 内置 Arctic Blue、Midnight Cyan、Warm Orange、Jade Mist、Violet Cloud 和 Graphite Amber 六套主题。所有主题共用相同的布局与交互，仅替换语义化颜色，兼顾浅色、深色和高对比使用场景。

The GUI includes Arctic Blue, Midnight Cyan, Warm Orange, Jade Mist, Violet Cloud, and Graphite Amber. Every theme shares the same layout and interaction model while replacing semantic colors for light, dark, and high-contrast use cases.

- Windows：点击窗口右上角的 `Theme` 按钮选择主题；设置保存在当前用户注册表中。
- Linux：使用窗口右上角的主题下拉框；设置保存在用户配置目录中。
- Windows: choose a theme from the `Theme` button in the upper-right corner; the choice is stored in the current user's registry.
- Linux: use the theme drop-down in the upper-right corner; the choice is stored in the user's configuration directory.

## 支持平台 / Supported platforms

Windows 7 SP1 及以上版本提供原生 Win32 GUI，默认构建 Windows x86 Release 版本；Linux 提供 GTK3 GUI；macOS 当前提供核心库和测试程序，不引入重量级 GUI 依赖。

Windows 7 SP1 and later provide a native Win32 GUI, normally built as an x86 Release target. Linux provides a GTK3 GUI. macOS currently builds the shared core and tests without a heavyweight GUI dependency.

## 构建与测试 / Build and test

### Windows

需要 Visual Studio 2019 或更高版本的 C++ 工具链、Windows SDK 和 CMake。/ Visual Studio 2019 or later C++ tools, the Windows SDK, and CMake are required.

```bat
cmake -S . -B build-win -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DFILEHASHTOOL_BUILD_BENCHMARK=ON
cmake --build build-win
ctest --test-dir build-win --output-on-failure
```

也可以在已打开 Visual Studio Developer Command Prompt 的 Windows 环境中运行 `tools\build-win-x86.cmd`。/ The helper `tools\build-win-x86.cmd` can also be run from a Visual Studio Developer Command Prompt.

### Linux

需要 GCC、CMake、Ninja、GTK3 和 OpenSSL 开发包。/ GCC, CMake, Ninja, GTK3, and OpenSSL development packages are required.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build libgtk-3-dev libssl-dev
cmake -S . -B build-linux -G Ninja -DFILEHASHTOOL_BUILD_GUI=ON -DFILEHASHTOOL_BUILD_SMOKE_TEST=ON -DFILEHASHTOOL_BUILD_BENCHMARK=ON
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

### macOS 核心 / macOS core

```bash
cmake -S . -B build-macos -G Ninja -DFILEHASHTOOL_BUILD_GUI=OFF -DFILEHASHTOOL_BUILD_SMOKE_TEST=ON
cmake --build build-macos --parallel
ctest --test-dir build-macos --output-on-failure
```

### 性能测试 / Benchmark

启用 `-DFILEHASHTOOL_BUILD_BENCHMARK=ON` 后运行：/ After enabling `-DFILEHASHTOOL_BUILD_BENCHMARK=ON`, run:

```text
filehash_benchmark <file> [md5|all] [buffer_mib]
```

其中 `buffer_mib` 是可选的读取缓冲区大小（MiB）。/ `buffer_mib` is the optional read-buffer size in MiB.

## 隐私与安全 / Privacy and security

程序在本地读取文件并计算校验值，不上传文件、不连接远程服务。复制结果可能包含敏感的绝对路径，请在分享前自行检查。

The application reads files and calculates hashes locally. It does not upload files or connect to a remote service. Copied results may contain sensitive absolute paths; review them before sharing.

## 当前状态与限制 / Status and limitations

1.1 版本在 1.0 的基础上加入两条记录的校验结果比对。当前仓库发布源码和构建配置，不跟踪 EXE、调试符号或构建目录；尚未提供安装程序。

Version 1.1 adds two-record hash-result comparison on top of the 1.0 hashing core, multi-file concurrency, cancellation semantics, and native GUIs. This repository publishes source code and build configuration only; executables, debug symbols, and build directories are not tracked. An installer is not included yet.

## 贡献 / Contributing

欢迎提交 Issue 和 Pull Request。请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。/ Issues and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first.

## 许可证 / License

本项目采用 MIT License，详见 [LICENSE](LICENSE)。/ This project is licensed under the MIT License; see [LICENSE](LICENSE).
