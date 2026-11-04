// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QListView;
class QPushButton;
class QStackedLayout;
class QStandardItem;
class QStandardItemModel;

/**
 * 妖怪ウォッチ専用ランチャー画面。
 *
 * GameList が持っている既存のモデル（フォルダ → ゲームのツリー）を読み取り、
 * 妖怪ウォッチ系タイトルだけを抜き出して、発売順のタイル一覧として表示する。
 * スキャン処理（ROMフォルダ・インストール済みタイトルの読み込み）は既存の
 * GameListWorker をそのまま使うので、このクラスは「絞り込みと表示」だけを担当する。
 */
class YokaiLauncher : public QWidget {
    Q_OBJECT

public:
    explicit YokaiLauncher(QStandardItemModel* source_model, QWidget* parent = nullptr);
    ~YokaiLauncher() override;

    /// ソースモデルを走査して一覧を作り直す。GameList の読み込み完了時に呼ぶ。
    void Rebuild();

    /// 見つかった妖怪ウォッチ系タイトルの数
    int TitleCount() const;

    /// タイトル名が妖怪ウォッチ系かどうか（テスト用に公開）
    static bool IsYokaiTitle(const QString& title);
    /// 発売順の並び順キー（小さいほど先）。該当しなければ 999。
    static int ReleaseOrder(const QString& title);

    /// ROMを置く専用フォルダ（~/Documents/YokaiWatch-ROMs）のパス
    static QString RomFolderPath();
    /// ROMフォルダと説明ファイルを作成する（既にあれば何もしない）
    static void EnsureRomFolder();
    /// ROMフォルダを Finder で開く
    static void OpenRomFolder();

signals:
    void GameChosen(const QString& game_path);
    void ShowAllGamesRequested();
    void AddDirectoryRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void CollectFrom(QStandardItem* parent);
    void OnActivated(const QModelIndex& index);
    void UpdateState();

    QStandardItemModel* source_model = nullptr; // GameList が所有。こちらでは読むだけ
    QStandardItemModel* tile_model = nullptr;   // ランチャー表示用のフラットなモデル

    QLabel* count_label = nullptr;
    QListView* tile_view = nullptr;
    QWidget* empty_page = nullptr;
    QStackedLayout* body = nullptr;
};
