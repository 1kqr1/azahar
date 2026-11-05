// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTextStream>
#include <QTimer>
#include "citra_qt/yokai_cia_autoinstall.h"
#include "citra_qt/yokai_launcher.h"

namespace {
// 記録ファイル名（ROMフォルダ直下。先頭がドットなので Finder では見えない）
const QString kRecordFile = QStringLiteral(".yw_installed_cia");
// 最終更新からこの秒数がたっていないファイルは「コピー中かも」として後回し
constexpr qint64 kSettleSeconds = 5;

bool IsCiaFile(const QFileInfo& info) {
    const QString ext = info.suffix().toLower();
    return ext == QStringLiteral("cia") || ext == QStringLiteral("zcia") ||
           ext == QStringLiteral("bcia");
}
} // namespace

YokaiCiaAutoInstaller::YokaiCiaAutoInstaller(QObject* parent) : QObject{parent} {
    YokaiLauncher::EnsureRomFolder();
    LoadRecord();

    scan_timer = new QTimer(this);
    scan_timer->setSingleShot(true);
    connect(scan_timer, &QTimer::timeout, this, &YokaiCiaAutoInstaller::Scan);

    // フォルダの中身が変わったら（ファイルを置いた・コピーが終わった等）スキャンし直す
    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] { ScheduleScan(); });
    RewatchDirectories();
}

YokaiCiaAutoInstaller::~YokaiCiaAutoInstaller() = default;

void YokaiCiaAutoInstaller::ScheduleScan(int delay_ms) {
    scan_timer->start(delay_ms);
}

bool YokaiCiaAutoInstaller::IsPending(const QString& path) const {
    return pending.contains(path);
}

QString YokaiCiaAutoInstaller::RecordKey(const QString& path) const {
    // 同じ名前でも中身を差し替えたら再インストールされるよう、サイズと更新日時も含める
    const QFileInfo info(path);
    const QString rel = QDir(YokaiLauncher::RomFolderPath()).relativeFilePath(path);
    return QStringLiteral("%1\t%2\t%3")
        .arg(rel)
        .arg(info.size())
        .arg(info.lastModified().toSecsSinceEpoch());
}

void YokaiCiaAutoInstaller::LoadRecord() {
    QFile file(QDir(YokaiLauncher::RomFolderPath()).filePath(kRecordFile));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (!line.isEmpty()) {
            done_keys.insert(line);
        }
    }
}

void YokaiCiaAutoInstaller::AppendRecord(const QString& key) {
    done_keys.insert(key);
    QFile file(QDir(YokaiLauncher::RomFolderPath()).filePath(kRecordFile));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write((key + QLatin1Char('\n')).toUtf8());
    }
}

void YokaiCiaAutoInstaller::RewatchDirectories() {
    // サブフォルダにも対応するため、フォルダ一式を監視対象にする
    const QStringList old = watcher->directories();
    if (!old.isEmpty()) {
        watcher->removePaths(old);
    }
    const QString root = YokaiLauncher::RomFolderPath();
    QStringList dirs{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && dirs.size() < 200) {
        dirs << it.next();
    }
    watcher->addPaths(dirs);
}

void YokaiCiaAutoInstaller::Scan() {
    RewatchDirectories();

    // ゲーム中・別のインストール中は待つ
    if (batch_running || (can_install && !can_install())) {
        ScheduleScan(5000);
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList ready;
    bool waiting_for_copy = false;

    QDirIterator it(YokaiLauncher::RomFolderPath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!IsCiaFile(info)) {
            continue;
        }
        const QString path = info.absoluteFilePath();
        if (done_keys.contains(RecordKey(path))) {
            continue; // 処理済み
        }
        // コピー中の判定：更新が新しすぎる、または前回スキャンからサイズが変わった
        const bool recent = now - info.lastModified().toSecsSinceEpoch() < kSettleSeconds;
        const bool size_changed = last_sizes.value(path, -1) != info.size();
        last_sizes[path] = info.size();
        if (recent || size_changed) {
            waiting_for_copy = true;
            continue;
        }
        ready << path;
    }

    if (waiting_for_copy) {
        ScheduleScan(3000);
    }
    if (ready.isEmpty()) {
        return;
    }

    batch_running = true;
    for (const auto& p : ready) {
        pending.insert(p);
    }
    emit InstallRequested(ready);
}

void YokaiCiaAutoInstaller::OnResult(const QString& path, bool done) {
    if (!pending.contains(path)) {
        return; // 手動インストールの結果なので関係なし
    }
    if (done) {
        AppendRecord(RecordKey(path));
    }
}

void YokaiCiaAutoInstaller::OnBatchFinished() {
    if (!batch_running) {
        return;
    }
    pending.clear();
    batch_running = false;
    // インストール中に新しく置かれたファイルがあれば拾う
    ScheduleScan();
}
