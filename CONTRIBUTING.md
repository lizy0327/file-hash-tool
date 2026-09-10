# 贡献指南 / Contributing Guide

感谢参与 Lizy File Hash Tool。中文在前，英文紧随其后。/ Thank you for contributing to Lizy File Hash Tool. Chinese comes first, followed by English.

## 开始之前 / Before you start

请先通过 Issue 描述问题或提议；小型修复可以直接提交 Pull Request。不要提交密码、私钥、内网地址、个人绝对路径或构建产物。

Please describe bugs or proposals in an Issue first; small fixes may go directly to a pull request. Do not commit passwords, private keys, internal addresses, personal absolute paths, or build artifacts.

## 开发流程 / Development flow

1. Fork 仓库并从 `main` 创建主题分支。/ Fork the repository and create a topic branch from `main`.
2. 使用 CMake 构建，并运行 `ctest`。/ Build with CMake and run `ctest`.
3. 修改后检查 Windows、Linux 或核心库对应的构建目标。/ Check the relevant Windows, Linux, or core-library target after changes.
4. 提交信息使用 Conventional Commits，例如 `fix: correct copied result paths`。/ Use Conventional Commits, for example `fix: correct copied result paths`.
5. Pull Request 的描述应说明变更、测试方式和平台影响。/ Explain the changes, tests, and platform impact in the pull request.

## 代码与文档约定 / Code and documentation conventions

- C++ 使用 C++17；保持现有警告级别可通过。/ Use C++17 and keep the existing warning levels clean.
- 注释、README、Issue 模板和用户可见文档均使用中英双语，中文在前。/ Comments, README files, issue templates, and user-facing documentation must be bilingual, with Chinese first.
- 共享核心不得依赖 GUI；平台相关实现放在对应目录。/ The shared core must not depend on a GUI; platform-specific code belongs in its platform directory.
- 涉及大文件、并发或取消逻辑时，应补充可重复的测试或基准说明。/ Add reproducible tests or benchmark notes for large-file, concurrency, or cancellation changes.

## Pull Request 检查清单 / Pull request checklist

- [ ] 中文说明在英文说明之前。/ Chinese text appears before English text.
- [ ] 已运行相关构建和测试。/ Relevant builds and tests have been run.
- [ ] 未包含密钥、凭据、内网信息或编译产物。/ No secrets, credentials, internal information, or compiled artifacts are included.
- [ ] 用户可见行为和兼容性影响已说明。/ User-visible behavior and compatibility impact are documented.
