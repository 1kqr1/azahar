// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <functional>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

/**
 * ROMフォルダ（~/Documents/YokaiWatch-ROMs）に置かれた .cia を見つけて、
 * 自動インストールを依頼するクラス。
 *
 * - 実際のインストールは GMainWindow::InstallCIA に任せる（既存の処理・進捗バーをそのまま使う）
 * - 一度処理したファイルは「ファイル名・サイズ・更新日時」をフォルダ内の .yw_installed_cia に記録し、
 *   次回からは飛ばす（同じファイルを毎回インストールしない）
 * - Finder でコピー中の巨大ファイルを途中でインストールしないよう、
 *   最終更新から数秒たっていて、サイズが2回続けて同じファイルだけを対象にする
 */
class YokaiCiaAutoInstaller : public QObject {
    Q_OBJECT

public:
    explicit YokaiCiaAutoInstaller(QObject* parent = nullptr);
    ~YokaiCiaAutoInstaller() override;

    /// 少し待ってからフォルダを調べる（連続した変更通知をまとめる）
    void ScheduleScan(int delay_ms = 1500);

    /// このパスが自動インストール中のものか
    bool IsPending(const QString& path) const;

    /// インストール結果を受け取る。done=true なら記録して次回から飛ばす
    void OnResult(const QString& path, bool done);

    /// 一連のインストールが終わった（GMainWindow::OnCIAInstallFinished から呼ぶ）
    void OnBatchFinished();

signals:
    /// これらの .cia をインストールしてほしい
    void InstallRequested(const QStringList& paths);

private:
    void Scan();
    void RewatchDirectories();
    QString RecordKey(const QString& path) const;
    void LoadRecord();
    void AppendRecord(const QString& key);

    QFileSystemWatcher* watcher = nullptr;
    QTimer* scan_timer = nullptr;
    QSet<QString> done_keys;       // 記録済み（インストール済み or 失敗確定）
    QSet<QString> pending;         // 今インストール中のパス
    QHash<QString, qint64> last_sizes; // コピー中判定用：前回スキャン時のサイズ
    bool batch_running = false;

public:
    /// GMainWindow 側から「今はインストールしてよいか」を判定する関数を渡す
    std::function<bool()> can_install;
};
