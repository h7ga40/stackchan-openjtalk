# StackChan OpenJTalk for K151-R

M5Stack StackChan K151-R上で、日本語テキストの解析、フルコンテキストラベル生成、HTS音声合成、スピーカー再生を行うArduinoプロジェクトです。

このリポジトリには、動作確認済みの省メモリ版HTS Engine、Open JTalk/MeCabフロントエンド、K151-R用ラッパーを含めています。ビルド時に外部のOpenJTalk作業フォルダを参照しません。

## 構成

- `firmware/StackChanOpenJTalk`: K151-R用スケッチ
- `libraries/OpenJTalkRawK151`: 省メモリ版HTS EngineのArduinoラッパー
- `libraries/OpenJTalkFrontendK151`: Open JTalk/MeCabフロントエンドのArduinoラッパー
- `libraries/StackChan-BSP`: M5Unified 0.2.21対応パッチを含むStackChan-BSP 1.1.0
- `vendor`: ビルドに必要なOpen JTalk、MeCab、HTS Engineのソース
- `assets`: Git管理する音声モデルとテストラベル
- `assets/cache`: Git管理しない約107 MBの辞書キャッシュ
- `tools`: セットアップ、ビルド、SD準備、発話用スクリプト

Arduino Library Managerから導入する汎用ライブラリは `.arduino/user/libraries/` に隔離され、Git管理されません。

取り込み元とライセンスについては [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。

## セットアップとビルド

PowerShellでこのリポジトリから実行します。

```powershell
.\tools\setup.ps1
.\tools\build.ps1 -Clean
```

生成物は `out/StackChanOpenJTalk/` に出力されます。

一時的に既存のArduino CLIキャッシュを再利用する場合は、`STACKCHAN_ARDUINO_STATE` にその `.arduino` フォルダを指定できます。通常は指定せず、このリポジトリ内の隔離環境を使用します。

```powershell
.\tools\Invoke-ArduinoCli.ps1 upload --verify `
  --fqbn m5stack:esp32:m5stack_cores3 `
  --port COM3 `
  --input-dir .\out\StackChanOpenJTalk `
  .\firmware\StackChanOpenJTalk
```

実際のポートは、次のコマンドで確認してください。

```powershell
.\tools\Invoke-ArduinoCli.ps1 board list
```

## SDカード

音声モデルとテストラベルだけを準備する場合:

```powershell
.\tools\prepare_sd.ps1 -DestinationRoot E:\ -Force
```

辞書も含める場合:

```powershell
.\tools\prepare_sd.ps1 -DestinationRoot E:\ -IncludeDictionary -Force
```

辞書はサイズが大きく、`sys.dic` がGitHubの通常の単一ファイル上限を超えるため、`assets/cache/open_jtalk_dic_utf_8-1.11/` はGit管理しません。現在の作業コピーには動作確認済み辞書を配置済みです。

## 発話

SDカードの辞書を読み込み、画面に `OpenJTalk READY`、シリアルに `FRONTEND=READY` と表示された後に実行します。

```powershell
.\tools\speak_openjtalk_device.ps1 -Port COM3 -Pitch 3 `
  -Text 'スタックチャンの中で文章を解析しています。'
```

ピッチは `-12` から `12` の範囲で指定できます。Arduino CLIのシリアルモニターは、発話スクリプトを実行する前に閉じてください。
