# OBS Plugin Template

## Introduction

The plugin template is meant to be used as a starting point for OBS Studio plugin development. It includes:

* Boilerplate plugin source code
* A CMake project file
* GitHub Actions workflows and repository actions

## Supported Build Environments

| Platform  | Tool   |
|-----------|--------|
| Windows   | Visual Studio 17 2022 |
| macOS     | XCode 16.0 |
| Windows, macOS  | CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3 |
| Ubuntu 24.04 | `ninja-build` |
| Ubuntu 24.04 | `pkg-config`
| Ubuntu 24.04 | `build-essential` |

## Quick Start

An absolute bare-bones [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide) is available in the wiki.

## Documentation

All documentation can be found in the [Plugin Template Wiki](https://github.com/obsproject/obs-plugintemplate/wiki).

Suggested reading to get up and running:

* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

## Kifu source settings

The custom `Dice Magnifier` source now exposes backend-oriented settings in OBS:

* `capture_source`: OBS video source to capture
* `enabled`: enable or pause backend polling/render updates

The backend endpoint is read from source `obs_data` key `backend_address`.
If the key is missing or empty, the plugin falls back to `127.0.0.1:50051`.
Request interval/timeout are persisted in `obs_data` and default to `500ms` each.
Stale fallback is enabled for up to about 1 second, then stale dice previews are cleared until fresh results arrive.

Phase 8 adds a background polling worker that watches these settings, logs backend poll attempts, and renders a small status strip so the source shows whether it is idle, polling, stale, or in an error state.

## 接続テスト手順

現時点の plugin 側は、backend 設定と監視ワーカーまで実装済みで、`SubmitFrame` の gRPC クライアント送受信はまだ入っていません。
そのため、次の手順は end-to-end 接続を確認するための実行手順として README に残し、transport 実装後にそのまま使える形にしています。

1. `kifume-inference` を `127.0.0.1:50051` で起動します。
2. OBS を起動し、この plugin を読み込みます。
3. Scene に `Dice Magnifier` を追加します。
4. `capture_source` を選択し、`enabled` を有効化します。
5. backend の応答が遅いとき、前回結果へのフォールバック表示が約1秒維持された後に消えることを確認します。
6. plugin のログで backend worker の起動と poll の状態遷移を確認します。
7. inference 側に `SubmitFrame` の受信ログが出ることを確認します。

補足: backend の接続先を変更する場合は source の `obs_data` に `backend_address` を設定してください。

補足:

* 現在の Phase 8 では、worker は backend 宛の poll スケジュールを持ちますが、実際の frame 送信はまだ未実装です。
* そのため、手順 8 の確認は transport 実装後の最終確認項目です。

## GitHub Actions & CI

Default GitHub Actions workflows are available for the following repository actions:

* `push`: Run for commits or tags pushed to `master` or `main` branches.
* `pr-pull`: Run when a Pull Request has been pushed or synchronized.
* `dispatch`: Run when triggered by the workflow dispatch in GitHub's user interface.
* `build-project`: Builds the actual project and is triggered by other workflows.
* `check-format`: Checks CMake and plugin source code formatting and is triggered by other workflows.

The workflows make use of GitHub repository actions (contained in `.github/actions`) and build scripts (contained in `.github/scripts`) which are not needed for local development, but might need to be adjusted if additional/different steps are required to build the plugin.

### Retrieving build artifacts

Successful builds on GitHub Actions will produce build artifacts that can be downloaded for testing. These artifacts are commonly simple archives and will not contain package installers or installation programs.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the correspodning [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
