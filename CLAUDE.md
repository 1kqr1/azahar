# Azahar YW Edition（妖怪ウォッチ専用版）引き継ぎメモ

Azahar（3DSエミュレーター）をフォークし、妖怪ウォッチ専用ランチャーと macOS 向け Metal 描画を加えた個人用ビルド。
作業ブランチは `yokai-edition`。リモートは `origin` = 1kqr1/azahar、`upstream` = azahar-emu/azahar。

## 開発者・環境
- 開発者は日本語話者。説明・コメントは日本語で、何をなぜやったかまで説明する
- 開発機は M3 MacBook Air（Apple Silicon / arm64）。ターゲットも macOS arm64
- ゲームは妖怪ウォッチ2真打などをプレイ中。ROM は本人が所有するものを使い、リポジトリや配布物には絶対に含めない

## これまでの変更（コミット3つ）
1. **妖怪ウォッチ ランチャー** `src/citra_qt/yokai_launcher.{h,cpp}`
   - GameList の既存モデル（item_model）を読み、タイトル名に「妖怪ウォッチ」「妖怪三国志」「Yo-kai Watch」を含むアプリ本体（Program ID 上位が 0x00040000）だけをタイル表示
   - 発売順ソート（無印→2 元祖/本家/真打→バスターズ→3→三国志）。NFKC 正規化で全角数字に対応
   - お気に入り欄との重複はパスで除去。SMDH の 48px アイコンを 3 倍にして、スムージングなしで描画
   - `GameList` に QStackedLayout を追加。page0 がランチャー、page1 が従来のリスト（「すべてのソフトを表示」/「← ランチャーに戻る」で切り替え）
   - `GameList::DonePopulating()` の最後で `launcher->Rebuild()`
2. **ROM フォルダ**
   - `YokaiLauncher::RomFolderPath()` = `~/Documents/YokaiWatch-ROMs`。起動時に作成し、説明 txt を置く
   - `configuration/config.cpp` の game_dirs 読み込み後に、このフォルダを deep_scan で必ず追加
   - ランチャー右上に「ROMフォルダを開く」ボタン
3. **Metal 描画（MoltenVK 経由）**
   - `common/settings.h`: macOS の既定 API を Vulkan に変更
   - `common/settings.cpp` `GetWorkingGraphicsAPI()`: macOS では OpenGL 設定を Vulkan に読み替え（Vulkan → MoltenVK → Metal）
   - `configuration/configure_graphics.cpp`: macOS では Vulkan の表示名を「Metal (MoltenVK)」にし、OpenGL を選択不可に
4. **DMG ビルド**
   - `.github/workflows/yokai-macos.yml`: push / 手動実行で arm64 ビルド → `Azahar YW Edition.app` → ad-hoc 署名 → DMG を Artifact として出力
   - 本家の build.yml / format.yml / libretro.yml はフォークでは不要なので削除済み
   - `dist/yokai/ROMの入れ方.txt` を DMG に同梱

5. **ROMフォルダの .cia 自動インストール** `src/citra_qt/yokai_cia_autoinstall.{h,cpp}`
   - ROMフォルダ（サブフォルダ含む）を監視し、新しい .cia/.zcia/.bcia を GMainWindow::InstallCIA に渡す
   - 処理済みは `.yw_installed_cia`（名前・サイズ・更新日時）に記録。コピー中・ゲーム中は待つ
6. **妖怪ウォッチ2 ローカル通信修正**（utosa123/azahar の yw2-local-play-v3-beta、fbd3fb0..2b9131b を取り込み）
   - nwm_uds / kernel / svc の変更。対戦・交換・バスターズ用。本家側はベータ版

7. **Windows / Android ビルド** `.github/workflows/yokai-windows-android.yml`
   - Windows: MSVC・FFmpeg なしで zip。ランチャー等の Qt 機能も入る
   - Android: relWithDebInfoLite（パッケージ名 .debug 付き・デバッグ署名）。公式版と並べて入れられる。Android は独自UIのためランチャー等はなく、通信修正のみ
   - 説明書: `dist/yokai/通信のやり方.txt`、`dist/yokai/Windowsでの使い方.txt`

## ローカルビルド（Mac）
```bash
brew install cmake ninja spirv-tools ccache
git submodule update --init --recursive
cmake -B build/arm64 -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DENABLE_ROOM_STANDALONE=OFF -DENABLE_FFMPEG=OFF -DENABLE_VULKAN=ON
ninja -C build/arm64 && ninja -C build/arm64 bundle
# 実行: open build/arm64/bundle/azahar.app
```

## 未確認・注意
- macOS での実ビルドと実機起動はまだ確認していない。これまでの確認は、Linux 上での構文チェックとランチャーの描画テスト（ダミーデータ）のみ。最初の作業は「ビルドが通るか」「ランチャーにタイトルが出るか」の確認
- SMDH のタイトル言語はシステム言語設定で変わる。日本版 ROM で日本語タイトルが取れているか要確認
- Documents フォルダへのアクセス時に macOS のプライバシー確認が出る
- 設定・セーブデータの保存場所は本家 Azahar と共通（アプリ名が同じため）

## 今後の候補
- コントローラー（自作の「3DS Pad」= Android スマホを下画面＋ボタンにするツール）でランチャーを操作できるようにする
- ROM フォルダの監視・ランチャー上の「再読み込み」ボタン
- ネイティブ Metal バックエンド（MoltenVK を使わない実装）。renderer_vulkan（約14,500行）とシェーダー生成（MSL）の移植になり大規模。まず MoltenVK でのFPSを測り、ボトルネックが変換層だと分かってから着手する。進めるなら、画面表示部分から段階的に置き換える
- アプリ名・アイコンの独自化（配布する場合、「妖怪ウォッチ」の名称や公式画像は使わない）

## ルール
- 本家 azahar-emu/azahar へは PR を出さない（AI-POLICY.md で AI 生成コードの投稿が禁止されている）
- GPLv2 を維持する。ROM・BIOS・暗号鍵などのゲームデータはコミットしない
- リスクのある変更は別ブランチで試してから yokai-edition にマージする
