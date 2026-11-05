// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <array>
#include <vector>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QStackedLayout>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include "citra_qt/game_list_p.h"
#include "citra_qt/yokai_launcher.h"

namespace {

// タイル用モデルで使う独自ロール
constexpr int PathRole = Qt::UserRole + 100;
constexpr int OrderRole = Qt::UserRole + 101;
constexpr int SubtitleRole = Qt::UserRole + 102;

// 表示サイズ。SMDH の大アイコンは 48x48 のドット絵なので、整数倍(3倍)で拡大してにじませない
constexpr int IconSize = 144;
constexpr int TileWidth = 200;
constexpr int TileHeight = 252;

// 配色（ランチャー画面だけに適用。エミュ本体のテーマには影響しない）
const QColor BgColor{0x14, 0x17, 0x2b};
const QColor CardColor{0x22, 0x27, 0x45};
const QColor CardHover{0x2c, 0x33, 0x5a};
const QColor AccentColor{0xe8, 0x3a, 0x3a};
const QColor TextColor{0xf2, 0xf3, 0xf8};
const QColor SubTextColor{0x9a, 0xa0, 0xbf};

/// 比較用に正規化する。全角数字・全角英字を半角に(NFKC)、空白除去、小文字化。
/// SMDH のタイトルは「妖怪ウォッチ２ 真打」のように全角数字が入ることがあるため。
QString Normalize(const QString& s) {
    QString n = s.normalized(QString::NormalizationForm_KC).toLower();
    n.remove(QLatin1Char(' '));
    n.remove(QChar(0x3000)); // 全角スペース
    n.remove(QLatin1Char('\n'));
    return n;
}

struct OrderRule {
    const char* needle; // UTF-8、Normalize 済みの形で書く
    int order;
};

// 具体的なものから順に判定する（「妖怪ウォッチ」は全タイトルに含まれるので最後）
constexpr std::array<OrderRule, 21> kOrderRules{{
    {"元祖", 21},
    {"本家", 22},
    {"真打", 23},
    {"赤猫団", 31},
    {"白犬隊", 32},
    {"月兎組", 33},
    {"スシ", 41},
    {"テンプラ", 42},
    {"スキヤキ", 43},
    {"三国志", 51},
    // 海外版
    {"bonyspirits", 21},
    {"fleshysouls", 22},
    {"psychicspecters", 23},
    {"redcatcorps", 31},
    {"whitedogsquad", 32},
    {"moonrabbitcrew", 33},
    {"watch3", 41},
    {"ウォッチ3", 41},
    {"watch2", 20},
    {"ウォッチ2", 20},
    {"blasters", 30},
}};

/// タイル描画。カード状の角丸背景＋拡大アイコン＋タイトル2行＋サブ情報。
class TileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {TileWidth, TileHeight};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const QRectF card = QRectF(opt.rect).adjusted(6, 6, -6, -6);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;

        QPainterPath path;
        path.addRoundedRect(card, 14, 14);
        p->fillPath(path, hovered || selected ? CardHover : CardColor);
        if (selected) {
            p->setPen(QPen(AccentColor, 3));
            p->drawPath(path);
        }

        // アイコン：ドット絵がぼやけないよう、拡大はスムージングなし
        const QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
        const QRect icon_rect(int(card.center().x()) - IconSize / 2, int(card.top()) + 14,
                              IconSize, IconSize);
        if (!icon.isNull()) {
            p->setRenderHint(QPainter::SmoothPixmapTransform, false);
            p->drawPixmap(icon_rect, icon);
        }

        // タイトル（最大2行、はみ出しは省略）
        QFont title_font = opt.font;
        title_font.setBold(true);
        title_font.setPointSizeF(std::max(10.0, opt.font.pointSizeF() * 1.05));
        p->setFont(title_font);
        p->setPen(TextColor);
        const QRectF text_rect(card.left() + 10, icon_rect.bottom() + 8, card.width() - 20, 40);
        const QString title = index.data(Qt::DisplayRole).toString();
        const QFontMetrics fm(title_font);
        const QString elided = fm.elidedText(title, Qt::ElideRight, int(text_rect.width()) * 2);
        p->drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, elided);

        QFont sub_font = opt.font;
        sub_font.setPointSizeF(std::max(8.0, opt.font.pointSizeF() * 0.85));
        p->setFont(sub_font);
        p->setPen(SubTextColor);
        const QRectF sub_rect(card.left() + 10, card.bottom() - 22, card.width() - 20, 16);
        p->drawText(sub_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                    index.data(SubtitleRole).toString());

        p->restore();
    }
};

} // namespace

bool YokaiLauncher::IsYokaiTitle(const QString& title) {
    const QString n = Normalize(title);
    return n.contains(QStringLiteral("妖怪ウォッチ")) || n.contains(QStringLiteral("妖怪三国志")) ||
           n.contains(QStringLiteral("yo-kaiwatch")) || n.contains(QStringLiteral("yokaiwatch"));
}

int YokaiLauncher::ReleaseOrder(const QString& title) {
    const QString n = Normalize(title);
    for (const auto& rule : kOrderRules) {
        if (n.contains(QString::fromUtf8(rule.needle))) {
            return rule.order;
        }
    }
    return IsYokaiTitle(title) ? 10 : 999; // 無印「妖怪ウォッチ」
}

QString YokaiLauncher::RomFolderPath() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(QStringLiteral("YokaiWatch-ROMs"));
}

void YokaiLauncher::EnsureRomFolder() {
    QDir dir(RomFolderPath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return;
    }
    QFile readme(dir.filePath(QStringLiteral("ここにROMを入れてください.txt")));
    if (readme.exists() || !readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    readme.write(QStringLiteral(
        "このフォルダに、自分で吸い出した妖怪ウォッチのROMを入れてください。\n"
        "対応形式: .3ds / .cci / .cxi / .3dsx / .app / .elf / .axf\n"
        ".cia（更新データ・追加コンテンツも）は、ここに置くと自動でインストールされます。\n"
        "インストール済みの .cia は、あとで消しても遊べます。\n"
        "\n"
        "入れたらアプリを開き直すか、ランチャーの「再読み込み」を押すと一覧に出ます。\n"
        "サブフォルダに分けても読み込まれます。\n")
                     .toUtf8());
}

void YokaiLauncher::OpenRomFolder() {
    EnsureRomFolder();
    QDesktopServices::openUrl(QUrl::fromLocalFile(RomFolderPath()));
}

YokaiLauncher::YokaiLauncher(QStandardItemModel* source_model_, QWidget* parent)
    : QWidget{parent}, source_model{source_model_} {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, BgColor);
    pal.setColor(QPalette::WindowText, TextColor);
    setPalette(pal);

    // ── ヘッダー ──────────────────────────────
    auto* title = new QLabel(QStringLiteral("妖怪ウォッチ ランチャー"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.6);
    tf.setBold(true);
    title->setFont(tf);

    count_label = new QLabel;
    count_label->setStyleSheet(QStringLiteral("color: #9aa0bf;"));

    const QString button_style = QStringLiteral(
        "QPushButton { color: #f2f3f8; background: #2c335a; border: none; border-radius: 8px;"
        "  padding: 6px 14px; }"
        "QPushButton:hover { background: #e83a3a; }");

    auto* show_all = new QPushButton(QStringLiteral("すべてのソフトを表示"));
    show_all->setStyleSheet(button_style);
    show_all->setCursor(Qt::PointingHandCursor);
    connect(show_all, &QPushButton::clicked, this, &YokaiLauncher::ShowAllGamesRequested);

    auto* open_roms = new QPushButton(QStringLiteral("ROMフォルダを開く"));
    open_roms->setStyleSheet(button_style);
    open_roms->setCursor(Qt::PointingHandCursor);
    connect(open_roms, &QPushButton::clicked, this, [] { YokaiLauncher::OpenRomFolder(); });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(24, 18, 24, 6);
    header->addWidget(title);
    header->addSpacing(12);
    header->addWidget(count_label, 0, Qt::AlignBottom);
    header->addStretch();
    header->addWidget(open_roms);
    header->addSpacing(8);
    header->addWidget(show_all);

    // ── タイル一覧 ────────────────────────────
    tile_model = new QStandardItemModel(this);
    tile_model->setSortRole(OrderRole);

    tile_view = new QListView;
    tile_view->setModel(tile_model);
    tile_view->setViewMode(QListView::IconMode);
    tile_view->setResizeMode(QListView::Adjust);
    tile_view->setMovement(QListView::Static);
    tile_view->setUniformItemSizes(true);
    tile_view->setGridSize({TileWidth, TileHeight});
    tile_view->setSpacing(0);
    tile_view->setWordWrap(true);
    tile_view->setMouseTracking(true);
    tile_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tile_view->setSelectionMode(QAbstractItemView::SingleSelection);
    tile_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tile_view->setFrameShape(QFrame::NoFrame);
    tile_view->setItemDelegate(new TileDelegate(tile_view));
    tile_view->setStyleSheet(
        QStringLiteral("QListView { background: transparent; padding: 12px 18px; }"
                       "QScrollBar:vertical { background: transparent; width: 10px; }"
                       "QScrollBar::handle:vertical { background: #2c335a; border-radius: 5px; }"
                       "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"));
    // ダブルクリック / Enter で起動
    connect(tile_view, &QListView::activated, this, &YokaiLauncher::OnActivated);

    // ── 見つからなかった時の画面 ───────────────
    empty_page = new QWidget;
    auto* empty_layout = new QVBoxLayout(empty_page);
    auto* empty_text = new QLabel(
        QStringLiteral("妖怪ウォッチのソフトが見つかりません。\n"
                       "下のボタンでROMフォルダを開き、ROMを入れてからアプリを開き直してください。\n\n%1")
            .arg(QDir::toNativeSeparators(RomFolderPath())));
    empty_text->setAlignment(Qt::AlignCenter);
    empty_text->setStyleSheet(QStringLiteral("color: #9aa0bf;"));
    auto* open_roms_big = new QPushButton(QStringLiteral("ROMフォルダを開く"));
    open_roms_big->setStyleSheet(button_style);
    open_roms_big->setCursor(Qt::PointingHandCursor);
    connect(open_roms_big, &QPushButton::clicked, this, [] { YokaiLauncher::OpenRomFolder(); });
    auto* add_dir = new QPushButton(QStringLiteral("別のフォルダを追加"));
    add_dir->setStyleSheet(button_style);
    add_dir->setCursor(Qt::PointingHandCursor);
    connect(add_dir, &QPushButton::clicked, this, &YokaiLauncher::AddDirectoryRequested);
    empty_layout->addStretch();
    empty_layout->addWidget(empty_text);
    empty_layout->addSpacing(12);
    empty_layout->addWidget(open_roms_big, 0, Qt::AlignHCenter);
    empty_layout->addSpacing(6);
    empty_layout->addWidget(add_dir, 0, Qt::AlignHCenter);
    empty_layout->addStretch();

    auto* body_host = new QWidget;
    body = new QStackedLayout(body_host);
    body->addWidget(tile_view);
    body->addWidget(empty_page);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(header);
    root->addWidget(body_host, 1);

    UpdateState();
}

YokaiLauncher::~YokaiLauncher() = default;

int YokaiLauncher::TitleCount() const {
    return tile_model->rowCount();
}

void YokaiLauncher::Rebuild() {
    tile_model->clear();
    if (source_model) {
        CollectFrom(source_model->invisibleRootItem());
    }
    tile_model->sort(0);
    UpdateState();
}

void YokaiLauncher::CollectFrom(QStandardItem* parent) {
    // お気に入り欄などで同じソフトが二重に出るので、パスで重複を除く
    QSet<QString> seen;
    for (int r = 0; r < tile_model->rowCount(); ++r) {
        seen.insert(tile_model->item(r)->data(PathRole).toString());
    }

    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i, 0);
        if (!child) {
            continue;
        }
        const auto type = child->data(GameListItem::TypeRole).value<GameListItemType>();
        if (type != GameListItemType::Game) {
            // フォルダ系の行は中身を再帰的に見る
            if (child->hasChildren()) {
                CollectFrom(child);
                for (int r = 0; r < tile_model->rowCount(); ++r) {
                    seen.insert(tile_model->item(r)->data(PathRole).toString());
                }
            }
            continue;
        }

        const QString path = child->data(GameListItemPath::FullPathRole).toString();
        const QString name = child->data(GameListItemPath::TitleRole).toString();
        const QString long_name = child->data(GameListItemPath::LongTitleRole).toString();
        const u64 program_id = child->data(GameListItemPath::ProgramIdRole).toULongLong();

        // アプリ本体(00040000xxxxxxxx)のみ。更新データ・DLCは除外
        if ((program_id >> 32) != 0x00040000) {
            continue;
        }
        if (path.isEmpty() || seen.contains(path)) {
            continue;
        }
        if (!IsYokaiTitle(name) && !IsYokaiTitle(long_name)) {
            continue;
        }
        seen.insert(path);

        auto* tile = new QStandardItem;
        tile->setData(name.isEmpty() ? long_name : name, Qt::DisplayRole);
        tile->setData(child->data(Qt::DecorationRole), Qt::DecorationRole);
        tile->setData(path, PathRole);
        tile->setData(ReleaseOrder(name + long_name), OrderRole);
        tile->setData(QString::number(program_id, 16).toUpper().rightJustified(16, QLatin1Char('0')),
                      SubtitleRole);
        tile->setToolTip(long_name.isEmpty() ? name : long_name);
        tile_model->appendRow(tile);
    }
}

void YokaiLauncher::OnActivated(const QModelIndex& index) {
    const QString path = index.data(PathRole).toString();
    if (!path.isEmpty()) {
        emit GameChosen(path);
    }
}

void YokaiLauncher::UpdateState() {
    const int n = TitleCount();
    count_label->setText(n > 0 ? QStringLiteral("%1 本").arg(n) : QString());
    body->setCurrentWidget(n > 0 ? static_cast<QWidget*>(tile_view) : empty_page);
    if (n > 0 && !tile_view->currentIndex().isValid()) {
        tile_view->setCurrentIndex(tile_model->index(0, 0));
    }
}

void YokaiLauncher::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 表示されたら矢印キー＋Enterですぐ選べるようにフォーカスを渡す
    tile_view->setFocus();
}
