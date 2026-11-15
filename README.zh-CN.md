<div align="center">

<img src="assets/tux.png" alt="OmgZygisk" width="128">

# OmgZygisk

**面向 ARM64 Android 的用户态 Zygisk 实现。**

OmgZygisk 提供经典 Zygisk 模块加载、Native Zygisk Next（ZN）API、内置 WebUI 和可热插拔的 Functional Node（FN）模块，无需自定义内核模块。

**架构支持：仅 ARM64（`arm64-v8a`）。** 不提供 ARMv7（`armeabi-v7a`）、x86 或 x86_64 构建，也不支持 32 位进程注入。

[![Release](https://img.shields.io/github/v/release/pingranQi/Omgzygisk?label=最新版&color=brightgreen)](https://github.com/pingranQi/Omgzygisk/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

[English](README.md) · **简体中文** · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md)

[版本发布](https://github.com/pingranQi/Omgzygisk/releases) · [问题反馈](https://github.com/pingranQi/Omgzygisk/issues) · [源代码](https://github.com/pingranQi/Omgzygisk)

</div>

---

## 项目概览

OmgZygisk 基于 ptrace 用户态进程注入，面向常见 Android Root 方案，同时提供经典 Zygisk 模块接口和 Native ZN 兼容接口。

| 能力 | 说明 |
| :--- | :--- |
| **Root 方案** | APatch、KernelSU（包括 LKM 延迟加载）和 Magisk |
| **架构** | 仅 ARM64 / `arm64-v8a` |
| **模块 API** | 经典 Zygisk API、Native ZN API v1–v4 |
| **注入方式** | 用户态 ptrace，无需自定义内核模块 |
| **管理方式** | 内置 WebUI、Functional Node 模块 |
| **内存布局** | 优先选择带保护边界的高地址 VMA 空洞 |

## 功能特性

### 多 Root 方案支持

支持 **APatch**、**KernelSU** 和 **Magisk**。注入与模块加载运行在用户态，不依赖单独的内核模块。

### 经典 Zygisk 与 Native ZN API

除经典 Zygisk 模块外，还实现 Native ZN API v1–v4 兼容接口，包括：

- 通过 LSPlt 实现 PLT Hook，通过 Dobby 实现 Inline Hook。
- 符号解析器创建、查找与遍历。
- Companion 连接和 Runtime 查询。
- 根据 API 版本提供相应的兼容 API 表。

详见 [Native ZN API 与 HyperOS Runtime](docs/ZN_API.md)。

### HyperOS Runtime

包含面向 `hyos_spawner` 进程模型的 HyperOS Runtime 集成，可在应用专用化生命周期中触发 Native ZN Runtime 回调。

**验证状态：** 当前代码已通过构建和 CI 检查，但尚未在实体 HyperOS 设备上验证。不同 HyperOS 版本的私有 `hyos_spawner` 符号及 ABI 可能存在差异。在目标设备和系统版本完成测试前，请将此功能视为实验性实现。

### Functional Node（FN）模块

FN 模块通过声明式描述文件、进程作用域和触发点扩展经典模块机制：

- 启用或禁用节点无需重启；变更在下次匹配的进程 fork 时生效。
- 按应用和进程类型指定作用范围。
- 同时支持 Native 入口库和可选启动脚本。
- 可通过 WebUI 安装与管理。

详见 [FN 模块文档](docs/FN.md)。

### 内置 WebUI

WebUI 使用 React、Vite 和 Tailwind CSS 构建，通过 Root 管理器的 WebView 与 JavaScript Bridge 加载静态资源，不会监听网络端口。

提供运行状态、守护进程、Root 方案、Zygote 状态、模块管理、FN 节点、日志查看和外观设置等功能。详见 [WebUI 文档](docs/WEBUI.md)。

### DenyList 与挂载隔离

DenyList 使用两阶段策略，降低指定应用进程中 Root 与模块挂载的可见性：

| 策略 | 作用 |
| :--- | :--- |
| **Zygote 卸载** | 在应用专用化之前，从 zygote 移除选定的 Root 和模块挂载。 |
| **命名空间切换** | 作为回退方案，将应用进程切换到干净的挂载命名空间。 |

实际行为会受到 Root 方案、Android 版本和目标进程影响，请针对目标应用测试配置。

### 高地址 VMA 布局

注入加载器优先选择带保护边界的高地址虚拟内存空洞，以降低与现有内存映射发生冲突的可能性。

---

## 安装

1. 从 [GitHub Releases](https://github.com/pingranQi/Omgzygisk/releases/latest) 下载最新可刷入 ZIP。
2. 使用 APatch、KernelSU 或 Magisk 安装。
3. 重启设备。
4. 在 Root 管理器的模块页面打开 WebUI。

**设备要求：** ARM64 Android。安装包不包含 ARMv7、x86 或 x86_64 版本。

### 配置应用隔离

| Root 方案 | 设置 |
| :--- | :--- |
| **APatch / KernelSU** | 可针对目标应用启用 **Umount modules（卸载模块）**。 |
| **Magisk** | 配置 DenyList；使用 OmgZygisk 自身隔离流程时，保持 Magisk 自带的 **Enforce DenyList** 关闭。 |

具体行为可能因 Root 管理器和 Android 版本而异。

---

## 从源码构建

```sh
git clone https://github.com/pingranQi/Omgzygisk.git
cd Omgzygisk
./gradlew :module:zipRelease
```

可刷入 ZIP 输出到 `module/build/outputs/release/`。WebUI 构建需要 Node.js 和 pnpm，Gradle 会在模块打包流程中调用 WebUI 构建任务。

## 文档

- [Native ZN API 与 HyperOS Runtime](docs/ZN_API.md)
- [Functional Node 模块](docs/FN.md)
- [WebUI 架构与功能](docs/WEBUI.md)
- [自定义 Linker 集成](docs/CUSTOM_LINKER.md)
- [许可证与上游声明](NOTICE.md)

## 问题反馈

请通过 [GitHub Issues](https://github.com/pingranQi/Omgzygisk/issues) 提交可复现的问题或功能建议。请附上 Android 版本、设备 ABI、Root 方案及版本、OmgZygisk 构建版本和相关日志。请勿提交私钥、个人数据或敏感设备标识。

## 致谢

OmgZygisk 使用或基于以下开源项目构建：

- **Zygisk API** —— [topjohnwu](https://github.com/topjohnwu) / [Magisk](https://github.com/topjohnwu/Magisk)
- **Zygisk Next** —— [Dr-TSNG](https://github.com/Dr-TSNG/ZygiskNext)
- **NeoZygisk** —— [JingMatrix](https://github.com/JingMatrix/NeoZygisk)
- **LSPlt** —— PLT Hook 实现，保留其上游许可证。
- **Dobby** —— Inline Hook 实现，保留其上游许可证。

## 许可证

OmgZygisk 使用 [GNU Affero General Public License v3.0](LICENSE) 发布。仓库包含采用各自许可证的上游代码与第三方组件；相关版权和许可证声明均予以保留，详见 [NOTICE.md](NOTICE.md) 及各组件目录。