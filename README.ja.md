<div align="center">

<img src="assets/tux.png" alt="OmgZygisk" width="128">

# OmgZygisk

**ARM64 Android 向けのユーザー空間 Zygisk 実装。**

OmgZygisk は、従来の Zygisk モジュール読み込み、Native Zygisk Next（ZN）API、組み込み WebUI、ホットスワップ可能な Functional Node（FN）モジュールを提供します。独自のカーネルモジュールは不要です。

**対応アーキテクチャ：ARM64（`arm64-v8a`）のみ。** ARMv7（`armeabi-v7a`）、x86、x86_64 向けのビルドは提供されず、32 ビットプロセスへのインジェクションにも対応しません。

[![Release](https://img.shields.io/github/v/release/pingranQi/Omgzygisk?label=Latest&color=brightgreen)](https://github.com/pingranQi/Omgzygisk/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · **日本語**

[リリース](https://github.com/pingranQi/Omgzygisk/releases) · [Issue](https://github.com/pingranQi/Omgzygisk/issues) · [ソースコード](https://github.com/pingranQi/Omgzygisk)

</div>

---

## 概要

OmgZygisk は ptrace ベースのユーザー空間プロセスインジェクションを採用し、一般的な Android Root ソリューションに対応します。従来の Zygisk モジュールインターフェースと Native ZN 互換インターフェースの両方を提供します。

| 機能 | 詳細 |
| :--- | :--- |
| **Root ソリューション** | APatch、KernelSU（LKM late-load を含む）、Magisk |
| **アーキテクチャ** | ARM64 / `arm64-v8a` のみ |
| **モジュール API** | 従来の Zygisk API、Native ZN API v1–v4 |
| **インジェクション** | ユーザー空間 ptrace。独自カーネルモジュール不要 |
| **管理機能** | 組み込み WebUI、Functional Node モジュール |
| **メモリ配置** | ガード領域を持つ高位アドレスの VMA 空間を優先 |

## 主な機能

### Root ソリューションを問わない設計

**APatch**、**KernelSU**、**Magisk** に対応します。インジェクションとモジュール読み込みはユーザー空間で動作し、別途カーネルモジュールを必要としません。

### 従来の Zygisk API と Native ZN API

従来の Zygisk モジュールに加えて、Native ZN API v1–v4 の互換インターフェースを実装しています。

- LSPlt による PLT Hook と Dobby による Inline Hook。
- シンボルリゾルバーの作成、検索、列挙。
- Companion 接続と Runtime の取得。
- API バージョンに応じた互換 API テーブル。

詳細は [Native ZN API と HyperOS Runtime](docs/ZN_API.md) を参照してください。

### HyperOS Runtime

`hyos_spawner` のプロセスモデル向けに HyperOS Runtime を統合しています。アプリの specialize ライフサイクルにフックし、Native ZN Runtime コールバックを提供します。

**検証状況：** コードはビルドおよび CI チェック済みですが、実機の HyperOS デバイスでは検証されていません。非公開の `hyos_spawner` シンボルや ABI は HyperOS のバージョンによって異なる可能性があります。対象デバイスと OS バージョンでテストするまでは実験的な機能として扱ってください。

### Functional Node（FN）モジュール

FN モジュールは、宣言型 descriptor、プロセススコープ、trigger point によって従来のモジュール機構を拡張します。

- 再起動せずにノードを有効化・無効化できます。変更は次に対象となるプロセスの fork 時に反映されます。
- アプリやプロセスタイプごとに適用範囲を指定できます。
- Native エントリライブラリと任意の起動スクリプトに対応します。
- WebUI からインストール・管理できます。

詳細は [FN モジュールのドキュメント](docs/FN.md) を参照してください。

### 組み込み WebUI

WebUI は React、Vite、Tailwind CSS で構築されています。Root マネージャーの WebView と JavaScript Bridge を通じて静的コンテンツを読み込むため、ネットワークリスニングポートを開きません。

ランタイム、Daemon、Root ソリューション、Zygote の状態、モジュール管理、FN ノード、ログ表示、外観設定などを提供します。詳細は [WebUI ドキュメント](docs/WEBUI.md) を参照してください。

### DenyList とマウント分離

DenyList は二段階の方式で、対象アプリプロセスにおける Root とモジュールのマウント可視性を低減します。

| 方式 | 役割 |
| :--- | :--- |
| **Zygote unmount** | アプリの specialize 前に、選択した Root とモジュールのマウントを zygote から除去します。 |
| **Namespace switch** | フォールバックとして、アプリプロセスをクリーンなマウント名前空間へ切り替えます。 |

実際の挙動は Root ソリューション、Android バージョン、対象プロセスによって異なります。対象アプリで設定をテストしてください。

### 高位アドレス VMA の配置

インジェクションローダーは、既存のメモリマッピングとの衝突を減らすため、ガード領域を持つ高位アドレスの仮想メモリ空間を優先します。

---

## インストール

1. [GitHub Releases](https://github.com/pingranQi/Omgzygisk/releases/latest) から最新のフラッシュ可能な ZIP をダウンロードします。
2. APatch、KernelSU、または Magisk でインストールします。
3. デバイスを再起動します。
4. Root マネージャーのモジュールページから WebUI を開きます。

**デバイス要件：** ARM64 Android。ARMv7、x86、x86_64 向けのパッケージはありません。

### アプリ分離の設定

| Root ソリューション | 設定 |
| :--- | :--- |
| **APatch / KernelSU** | 必要に応じて対象アプリの **Umount modules** を有効にします。 |
| **Magisk** | DenyList を設定します。OmgZygisk 独自の分離機能を使用する場合、Magisk 側の **Enforce DenyList** は無効にしてください。 |

具体的な動作は Root マネージャーと Android のバージョンによって異なる場合があります。

---

## ソースからビルド

```sh
git clone https://github.com/pingranQi/Omgzygisk.git
cd Omgzygisk
./gradlew :module:zipRelease
```

フラッシュ可能な ZIP は `module/build/outputs/release/` に出力されます。WebUI のビルドには Node.js と pnpm が必要です。Gradle はモジュールのパッケージング時に WebUI のビルドを実行します。

## ドキュメント

- [Native ZN API と HyperOS Runtime](docs/ZN_API.md)
- [Functional Node モジュール](docs/FN.md)
- [WebUI の構成と機能](docs/WEBUI.md)
- [カスタム Linker の統合](docs/CUSTOM_LINKER.md)
- [ライセンスと上流の通知](NOTICE.md)

## サポートと不具合報告

再現可能な不具合や機能要望は [GitHub Issues](https://github.com/pingranQi/Omgzygisk/issues) に報告してください。Android バージョン、デバイス ABI、Root ソリューションとそのバージョン、OmgZygisk のビルド、関連ログを記載してください。秘密鍵、個人データ、機微なデバイス識別子は投稿しないでください。

## クレジット

OmgZygisk は以下のオープンソースプロジェクトを利用、またはその成果を基にしています。

- **Zygisk API** — [topjohnwu](https://github.com/topjohnwu) / [Magisk](https://github.com/topjohnwu/Magisk)
- **Zygisk Next** — [Dr-TSNG](https://github.com/Dr-TSNG/ZygiskNext)
- **NeoZygisk** — [JingMatrix](https://github.com/JingMatrix/NeoZygisk)
- **LSPlt** — PLT Hook 実装。上流のライセンスを保持しています。
- **Dobby** — Inline Hook 実装。上流のライセンスを保持しています。

## ライセンス

OmgZygisk は [GNU Affero General Public License v3.0](LICENSE) に基づいて配布されます。このリポジトリには、それぞれのライセンスに従う上流コードおよびサードパーティコンポーネントが含まれています。必要な著作権表示とライセンス通知は保持されています。詳細は [NOTICE.md](NOTICE.md) と各コンポーネントのディレクトリを参照してください。

既存インストールとの互換性のため、一部の内部モジュール識別子や端末上のデータパスでは `onyxzygisk` という識別子が引き続き使用される場合があります。
