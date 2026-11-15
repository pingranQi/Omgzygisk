<div align="center">

<img src="assets/tux.png" alt="OmgZygisk" width="128">

# OmgZygisk

**A userspace Zygisk implementation for ARM64 Android.**

OmgZygisk provides Zygisk-compatible module loading, Native Zygisk Next (ZN) API support, a built-in WebUI, and hot-swappable Functional Node (FN) modules — without requiring a custom kernel module.

**Supported architecture: ARM64 (`arm64-v8a`) only.** ARMv7 (`armeabi-v7a`), x86, and x86_64 are not packaged. 32-bit process injection is not supported.

[![Release](https://img.shields.io/github/v/release/pingranQi/Omgzygisk?label=Latest&color=brightgreen)](https://github.com/pingranQi/Omgzygisk/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

**English** · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md)

[Releases](https://github.com/pingranQi/Omgzygisk/releases) · [Issues](https://github.com/pingranQi/Omgzygisk/issues) · [Source](https://github.com/pingranQi/Omgzygisk)

</div>

---

## Overview

OmgZygisk is a userspace Zygisk implementation built around ptrace-based process injection. It is designed to work with common Android root solutions and provides both the classic Zygisk module interface and a Native ZN-compatible interface.

| Capability | Details |
| :--- | :--- |
| **Root solutions** | APatch, KernelSU (including LKM late-load), and Magisk |
| **Architecture** | ARM64 / `arm64-v8a` only |
| **Module APIs** | Classic Zygisk API and Native ZN API v1–v4 |
| **Injection** | Userspace ptrace; no custom kernel module |
| **Management** | Built-in WebUI and Functional Node modules |
| **Runtime placement** | Prefers guarded high-address VMA gaps for the injected library |

## Features

### Root-solution independent

Use OmgZygisk with **APatch**, **KernelSU**, or **Magisk**. The implementation runs in userspace and does not require a separate kernel module.

### Classic Zygisk and Native ZN APIs

OmgZygisk supports classic Zygisk modules and the Native ZN API through version 4. The Native ZN interface includes:

- PLT hooks through LSPlt and inline hooks through Dobby.
- Symbol resolver creation, lookup, and symbol iteration.
- Companion connection and runtime discovery.
- Version-aware API tables for Native ZN API v1–v4.

See [Native ZN API and HyperOS runtime](docs/ZN_API.md) for the implemented ABI and current validation limits.

### HyperOS runtime

A HyperOS runtime integration is included for the `hyos_spawner` process model. It hooks the relevant app-specialization lifecycle and exposes the Native ZN runtime callback.

**Validation status:** the code is build- and CI-checked, but has not been verified on a physical HyperOS device. Availability and ABI details of private `hyos_spawner` symbols may vary between HyperOS releases. Treat this integration as experimental until tested on the target device and OS version.

### Functional Node (FN) modules

FN modules extend the classic module model with declarative descriptors, process scopes, trigger points, and daemon-reported status.

- Enable or disable nodes without rebooting; changes take effect on the next applicable process fork.
- Target selected apps or process types.
- Combine native entry libraries with optional boot and post-fs-data scripts.
- Install FN packages through the WebUI.

See [FN module documentation](docs/FN.md) for the descriptor format, lifecycle, and package layout.

### Built-in WebUI

The WebUI is built with React, Vite, and Tailwind CSS. It is served as static module content through the root manager's WebView and JavaScript bridge; it does not open a network listening port.

The interface provides:

- Runtime, daemon, root-solution, and Zygote status.
- Installed Zygisk module information and hot-plug controls.
- FN node management.
- Filterable daemon and loader logs.
- Appearance, language, and runtime settings.

See [WebUI documentation](docs/WEBUI.md).

### DenyList and mount isolation

The DenyList uses a two-stage strategy to reduce root and module mount visibility in selected app processes:

| Strategy | Purpose |
| :--- | :--- |
| **Zygote unmount** | Removes selected root and module mounts before app specialization. |
| **Namespace switch** | Uses a clean mount namespace as a fallback isolation path. |

Behavior depends on the root solution, Android version, and target process. Test the configuration with the apps you intend to isolate.

### High-address VMA placement

The injected library loader prefers a guarded high-address virtual-memory gap when selecting a mapping location. This is intended to reduce collisions with existing mappings while keeping the placement strategy explicit.

---

## Installation

1. Download the latest flashable ZIP from [GitHub Releases](https://github.com/pingranQi/Omgzygisk/releases/latest).
2. Install it using APatch, KernelSU, or Magisk.
3. Reboot the device.
4. Open the module's WebUI from the root manager's module page.

**Device requirement:** ARM64 Android. The installer does not provide ARMv7, x86, or x86_64 builds.

### Configure app isolation

The relevant root-manager settings still apply:

| Root solution | Setting |
| :--- | :--- |
| **APatch / KernelSU** | Enable **Umount modules** for the target app where appropriate. |
| **Magisk** | Configure the DenyList. Keep Magisk's own **Enforce DenyList** disabled when using OmgZygisk's own isolation flow. |

Exact behavior can differ between root-manager and Android versions.

---

## Build from source

```sh
git clone https://github.com/pingranQi/Omgzygisk.git
cd Omgzygisk
./gradlew :module:zipRelease
```

The flashable ZIP is generated under:

```text
module/build/outputs/release/
```

The WebUI build requires Node.js and pnpm. Gradle invokes the WebUI build as part of the module packaging workflow.

---

## Documentation

- [Native ZN API and HyperOS runtime](docs/ZN_API.md)
- [Functional Node modules](docs/FN.md)
- [WebUI architecture and features](docs/WEBUI.md)
- [Custom linker integration](docs/CUSTOM_LINKER.md)
- [Licensing and upstream notices](NOTICE.md)

## Support and bug reports

Use [GitHub Issues](https://github.com/pingranQi/Omgzygisk/issues) for reproducible bug reports and feature requests. Include your Android version, device ABI, root solution and version, OmgZygisk build, and relevant logs. Do not post private keys, personal data, or sensitive device identifiers.

## Credits

OmgZygisk uses and builds on open-source work, including:

- **Zygisk API** — [topjohnwu](https://github.com/topjohnwu) and [Magisk](https://github.com/topjohnwu/Magisk).
- **Zygisk Next** — [Dr-TSNG](https://github.com/Dr-TSNG/ZygiskNext).
- **NeoZygisk** — [JingMatrix](https://github.com/JingMatrix/NeoZygisk).
- **LSPlt** — PLT hooking implementation, with its upstream license retained.
- **Dobby** — inline hooking implementation, with its upstream license retained.

## License

OmgZygisk is distributed under the [GNU Affero General Public License v3.0](LICENSE). The repository includes code from upstream projects and third-party components under their respective licenses. Their required copyright and license notices are retained; see [NOTICE.md](NOTICE.md) and the relevant component directories.
