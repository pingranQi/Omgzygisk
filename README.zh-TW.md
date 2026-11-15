<div align="center">

<img src="assets/tux.png" alt="OmgZygisk" width="128">

# OmgZygisk

**適用於 ARM64 Android 的使用者空間 Zygisk 實作。**

OmgZygisk 提供經典 Zygisk 模組載入、Native Zygisk Next（ZN）API、內建 WebUI，以及可熱抽換的 Functional Node（FN）模組，無需自訂核心模組。

**架構支援：僅 ARM64（`arm64-v8a`）。** 不提供 ARMv7（`armeabi-v7a`）、x86 或 x86_64 建置，也不支援 32 位元行程注入。

[![Release](https://img.shields.io/github/v/release/pingranQi/Omgzygisk?label=最新版&color=brightgreen)](https://github.com/pingranQi/Omgzygisk/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · **繁體中文** · [日本語](README.ja.md)

[版本發佈](https://github.com/pingranQi/Omgzygisk/releases) · [問題回報](https://github.com/pingranQi/Omgzygisk/issues) · [原始碼](https://github.com/pingranQi/Omgzygisk)

</div>

---

## 專案概覽

OmgZygisk 以 ptrace 使用者空間行程注入為核心，支援常見 Android Root 方案，同時提供經典 Zygisk 模組介面與 Native ZN 相容介面。

| 能力 | 說明 |
| :--- | :--- |
| **Root 方案** | APatch、KernelSU（包含 LKM 延遲載入）及 Magisk |
| **架構** | 僅 ARM64 / `arm64-v8a` |
| **模組 API** | 經典 Zygisk API、Native ZN API v1–v4 |
| **注入方式** | 使用者空間 ptrace，無需自訂核心模組 |
| **管理方式** | 內建 WebUI、Functional Node 模組 |
| **記憶體配置** | 優先使用具有保護邊界的高位址 VMA 空洞 |

## 功能特色

### 支援多種 Root 方案

支援 **APatch**、**KernelSU** 與 **Magisk**。注入與模組載入在使用者空間執行，不依賴獨立的核心模組。

### 經典 Zygisk 與 Native ZN API

除了經典 Zygisk 模組，也提供 Native ZN API v1–v4 相容介面，包括：

- 使用 LSPlt 進行 PLT Hook，並使用 Dobby 進行 Inline Hook。
- 符號解析器建立、查找與列舉。
- Companion 連線與 Runtime 查詢。
- 依 API 版本提供相應的相容 API 表。

詳見 [Native ZN API 與 HyperOS Runtime](docs/ZN_API.md)。

### HyperOS Runtime

包含針對 `hyos_spawner` 行程模型的 HyperOS Runtime 整合，可在應用程式專用化生命週期中觸發 Native ZN Runtime 回呼。

**驗證狀態：** 程式碼已通過建置與 CI 檢查，但尚未在實體 HyperOS 裝置上驗證。不同 HyperOS 版本的私有 `hyos_spawner` 符號與 ABI 可能不同。在目標裝置及系統版本完成測試前，請將此功能視為實驗性實作。

### Functional Node（FN）模組

FN 模組透過宣告式描述檔、行程作用範圍與觸發點擴充經典模組機制：

- 啟用或停用節點無需重新開機；變更會在下一次符合條件的行程 fork 時生效。
- 可依應用程式與行程類型指定作用範圍。
- 支援 Native 入口程式庫與選用的啟動指令碼。
- 可透過 WebUI 安裝與管理。

詳見 [FN 模組文件](docs/FN.md)。

### 內建 WebUI

WebUI 使用 React、Vite 與 Tailwind CSS 建置，透過 Root 管理器的 WebView 和 JavaScript Bridge 載入靜態資源，不會監聽網路連接埠。

提供執行狀態、Daemon、Root 方案、Zygote 狀態、模組管理、FN 節點、日誌檢視與外觀設定等功能。詳見 [WebUI 文件](docs/WEBUI.md)。

### DenyList 與掛載隔離

DenyList 使用兩階段策略，降低指定應用程式行程中 Root 與模組掛載的可見性：

| 策略 | 作用 |
| :--- | :--- |
| **Zygote 卸載** | 在應用程式專用化之前，從 zygote 移除選定的 Root 與模組掛載。 |
| **命名空間切換** | 作為備援方案，將應用程式行程切換至乾淨的掛載命名空間。 |

實際行為會受 Root 方案、Android 版本與目標行程影響，請針對目標應用程式測試設定。

### 高位址 VMA 配置

注入載入器優先選擇具有保護邊界的高位址虛擬記憶體空洞，以降低與既有記憶體映射衝突的可能性。

---

## 安裝

1. 從 [GitHub Releases](https://github.com/pingranQi/Omgzygisk/releases/latest) 下載最新可刷入 ZIP。
2. 使用 APatch、KernelSU 或 Magisk 安裝。
3. 重新啟動裝置。
4. 在 Root 管理器的模組頁面開啟 WebUI。

**裝置需求：** ARM64 Android。安裝包不包含 ARMv7、x86 或 x86_64 版本。

### 設定應用程式隔離

| Root 方案 | 設定 |
| :--- | :--- |
| **APatch / KernelSU** | 可針對目標應用程式啟用 **Umount modules（卸載模組）**。 |
| **Magisk** | 設定 DenyList；使用 OmgZygisk 自有隔離流程時，請保持 Magisk 內建的 **Enforce DenyList** 關閉。 |

具體行為可能因 Root 管理器與 Android 版本而異。

---

## 從原始碼建置

```sh
git clone https://github.com/pingranQi/Omgzygisk.git
cd Omgzygisk
./gradlew :module:zipRelease
```

可刷入 ZIP 會輸出至 `module/build/outputs/release/`。WebUI 建置需要 Node.js 與 pnpm；Gradle 會在模組封裝流程中呼叫 WebUI 建置工作。

## 文件

- [Native ZN API 與 HyperOS Runtime](docs/ZN_API.md)
- [Functional Node 模組](docs/FN.md)
- [WebUI 架構與功能](docs/WEBUI.md)
- [自訂 Linker 整合](docs/CUSTOM_LINKER.md)
- [授權與上游聲明](NOTICE.md)

## 問題回報

請透過 [GitHub Issues](https://github.com/pingranQi/Omgzygisk/issues) 提交可重現的問題或功能建議。請附上 Android 版本、裝置 ABI、Root 方案及版本、OmgZygisk 建置版本與相關日誌。請勿提交私鑰、個人資料或敏感裝置識別資訊。

## 致謝

OmgZygisk 使用或基於以下開源專案建置：

- **Zygisk API** —— [topjohnwu](https://github.com/topjohnwu) / [Magisk](https://github.com/topjohnwu/Magisk)
- **Zygisk Next** —— [Dr-TSNG](https://github.com/Dr-TSNG/ZygiskNext)
- **NeoZygisk** —— [JingMatrix](https://github.com/JingMatrix/NeoZygisk)
- **LSPlt** —— PLT Hook 實作，保留其上游授權條款。
- **Dobby** —— Inline Hook 實作，保留其上游授權條款。

## 授權條款

OmgZygisk 依 [GNU Affero General Public License v3.0](LICENSE) 發佈。此儲存庫包含採用各自授權條款的上游程式碼與第三方元件；相關著作權與授權聲明均予以保留，詳見 [NOTICE.md](NOTICE.md) 及各元件目錄。

為相容既有安裝，部分內部模組識別碼與裝置資料目錄仍可能使用 `onyxzygisk` 識別字。
