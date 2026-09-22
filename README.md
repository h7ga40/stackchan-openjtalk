# StackChan OpenJTalk for K151-R

M5Stack StackChan K151-R上で、日本語テキストの解析、フルコンテキストラベル生成、HTS音声合成、スピーカー再生を行うArduinoプロジェクトです。

省メモリ版HTS Engine、Open JTalk/MeCabフロントエンド、K151-R用ラッパーをリポジトリ内に収録しているため、外部のOpenJTalkソースツリーは不要です。日本語のラベル生成もM5Stack側で実行します。

## 動作確認済み環境

- Hardware: M5Stack StackChan K151-R（M5CoreS3）
- OS: Windows
- Shell: Windows PowerShell 5.1またはPowerShell 7
- Arduino CLI: 1.5.1
- Board package: M5Stack ESP32 3.2.5
- FQBN: `m5stack:esp32:m5stack_cores3`
- microSD: FAT32、Open JTalk辞書を含める場合は空き容量128 MB以上を推奨

実機で、ビルド、書き込み、SDカード読み込み、M5Stack側でのラベル生成、連続発話まで確認しています。測定結果は [docs/BUILD_RESULTS.md](docs/BUILD_RESULTS.md) に記録しています。

## 必要なアプリ

次のいずれかをインストールしてください。

1. [Arduino IDE 2.x](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/01.ide-v2-downloading-and-installing/)
2. [Arduino CLI](https://docs.arduino.cc/arduino-cli/installation/) をインストールし、`arduino-cli.exe` を `PATH` に追加

付属スクリプトは、最初に `PATH` 上の `arduino-cli.exe` を探し、見つからない場合はWindows版Arduino IDE 2.xに同梱されたArduino CLIを使用します。

リポジトリをGitで取得する場合はGitも必要です。GitHubのZIPを展開して利用する場合、Gitは不要です。Python、PC版Open JTalk、MeCabの個別インストールは必要ありません。

### PowerShellと文字コード

日本語テキストとデバイスのシリアル通信にはUTF-8を使用します。付属の発話スクリプトはシリアルポートの文字コードをUTF-8へ明示的に設定するため、通常はコードページの変更は必要ありません。

Windows PowerShell 5.1で、日本語などの非ASCII文字を直接記述した独自の `.ps1` ファイルを作る場合は、UTF-8 BOM付きで保存してください。PowerShell 7ではUTF-8 BOMなしも正しく扱えます。Shift_JISへの変換や、システム既定の文字コードに依存したファイル入出力は避けてください。

## ボードマネージャ

付属の `tools/setup.ps1` が次のURLとバージョンを自動設定します。

```text
Board Manager URL:
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json

Package:
m5stack:esp32@3.2.5

Board/FQBN:
M5CoreS3 / m5stack:esp32:m5stack_cores3
```

Arduino IDEで手動設定する場合は、`File` → `Preferences` → `Additional Board Manager URLs` に上記URLを追加し、Boards Managerで `M5Stack` 3.2.5をインストールします。その後、ボードとして `M5CoreS3` を選択します。M5Stack公式の手順は [Arduino Board Management](https://docs.m5stack.com/en/arduino/arduino_board) を参照してください。

## Arduinoライブラリ

`tools/setup.ps1` は以下の固定バージョンを、リポジトリ内の隔離領域 `.arduino/user/libraries/` へインストールします。システム全体のArduino環境は変更しません。

| Library | Version | Purpose |
| --- | ---: | --- |
| M5Unified | 0.2.21 | CoreS3の画面、SD、スピーカー、電源制御 |
| M5GFX | 0.2.28 | 画面描画 |
| IRremoteESP8266 | 2.9.0 | StackChan-BSP依存関係 |
| M5Unit-NFC | 0.1.1 | StackChan-BSP依存関係 |
| M5UnitUnified | 0.5.5 | M5Stack Unit共通基盤 |
| M5Utility | 0.2.0 | M5Stack共通ユーティリティ |
| M5HAL | 0.1.2 | M5Stackハードウェア抽象化 |

次のプロジェクト固有ライブラリはリポジトリ内に含まれているため、Library Managerから追加しないでください。

- `OpenJTalkRawK151`: 省メモリ版HTS Engineラッパー
- `OpenJTalkFrontendK151`: Open JTalk/MeCabラベル生成ラッパー
- `StackChan-BSP` 1.1.0: M5Unified 0.2.21対応パッチを含む固定版

## セットアップとビルド

PowerShellでリポジトリのルートから実行します。初回セットアップではM5Stackのツールチェーンをダウンロードするため、時間と数GBの空き容量が必要です。

```powershell
.\tools\setup.ps1
.\tools\build.ps1 -Clean
```

生成物は `out/StackChanOpenJTalk/` に出力されます。Arduino環境、ダウンロードキャッシュ、生成物はGit管理されません。

既存のArduino CLIキャッシュを一時的に再利用する場合は、`STACKCHAN_ARDUINO_STATE` にその状態フォルダを指定できます。通常は指定せず、このリポジトリ内の `.arduino/` を使用します。

## Open JTalk辞書の準備

日本語テキストをM5Stack側で解析するには、Open JTalk 1.11のUTF-8辞書が必要です。辞書は約107 MBあり、`sys.dic` がGitHubの通常の単一ファイル上限を超えるため、このリポジトリには含めていません。

1. Open JTalk公式サイトの [Dictionary for Open JTalk version 1.11 — Binary Package (UTF-8)](https://downloads.sourceforge.net/open-jtalk/open_jtalk_dic_utf_8-1.11.tar.gz) を取得します。
2. `open_jtalk_dic_utf_8-1.11.tar.gz` を展開します。
3. 展開されたフォルダを次の場所へ置きます。

```text
assets/cache/open_jtalk_dic_utf_8-1.11/
├─ char.bin
├─ matrix.bin
├─ sys.dic
├─ unk.dic
└─ ...
```

PowerShellとWindows標準の`tar`を使用する例:

```powershell
New-Item -ItemType Directory -Force .\assets\cache | Out-Null
tar -xf "$env:USERPROFILE\Downloads\open_jtalk_dic_utf_8-1.11.tar.gz" `
  -C .\assets\cache
```

`assets/cache/` は `.gitignore` の対象です。辞書のライセンスファイル `COPYING` も削除せず、そのまま保持してください。

## SDカードの準備

FAT32でフォーマットしたmicroSDをPCへ接続し、ドライブ文字を確認します。以下は `E:\` の例です。

```powershell
.\tools\prepare_sd.ps1 -DestinationRoot E:\ -IncludeDictionary -Force
```

次のファイルが作成されます。

```text
/openjtalk/mei_normal_16.raw
/openjtalk/test.lab
/openjtalk/dic/...
```

音声モデルとテストラベルのみコピーする場合は、`-IncludeDictionary` を省略できます。

## 書き込み

データ通信対応USB-CケーブルでStackChanを接続し、ポートを確認します。

```powershell
.\tools\Invoke-ArduinoCli.ps1 board list
```

`COM3`へ書き込む例:

```powershell
.\tools\Invoke-ArduinoCli.ps1 upload --verify `
  --fqbn m5stack:esp32:m5stack_cores3 `
  --port COM3 `
  --input-dir .\out\StackChanOpenJTalk `
  .\firmware\StackChanOpenJTalk
```

Arduino CLIでボード名が `Unknown` と表示されても、COMポートとFQBNが正しければ書き込みできます。書き込みは工場出荷時ファームウェアを置き換えます。

## 発話

microSDをStackChanへ挿入して起動します。画面に `OpenJTalk READY`、シリアルログに `FRONTEND=READY` と表示されるまで待ちます。辞書の初期化には約1分かかる場合があります。

```powershell
.\tools\speak_openjtalk_device.ps1 -Port COM3 -Pitch 3 `
  -Text 'スタックチャンの中で文章を解析しています。'
```

ピッチは `-12` から `12` の範囲で指定できます。シリアルポートを同時に開けるのは1プロセスだけなので、発話スクリプトを実行する前にArduino CLIやArduino IDEのシリアルモニターを閉じてください。

## リポジトリ構成

- `AGENTS.md`: Codexが変更時に従うプロジェクト固有の指示
- `firmware/StackChanOpenJTalk`: K151-R用スケッチ
- `libraries/OpenJTalkRawK151`: 省メモリ版HTS EngineのArduinoラッパー
- `libraries/OpenJTalkFrontendK151`: Open JTalk/MeCabフロントエンドのArduinoラッパー
- `libraries/StackChan-BSP`: パッチ済みStackChan-BSP 1.1.0
- `vendor`: ビルドに必要なOpen JTalk、MeCab、HTS Engineソース
- `assets`: 音声モデル、帰属表示、テストラベル
- `assets/cache`: Git管理しない辞書キャッシュ
- `tools`: セットアップ、ビルド、SD準備、書き込み、発話用スクリプト

## ライセンスと公開について

同梱する第三者成果物は、それぞれの条件を満たすことで再配布できます。

- Open JTalk: Modified BSD
- HTS Engine API: Modified BSD
- MeCab: BSDを選択して再配布
- StackChan-BSP: MIT
- HTS Voice Mei: Creative Commons Attribution 3.0

著作権表示、免責条項、音声モデルの帰属表示を削除しないでください。詳細と取り込み元は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)、個別の本文は `LICENSES/`、`assets/`、`libraries/StackChan-BSP/LICENSE` を参照してください。

このリポジトリ固有の統合コードは [BSD 3-Clause License](LICENSE) で公開します。第三者成果物には、それぞれのライセンス条件が優先して適用されます。
