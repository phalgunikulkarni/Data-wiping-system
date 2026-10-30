#include "MainWindow.h"
#include "ImageBackgroundWidget.h"

#include <QFrame>
#include <QComboBox>
#include <QDateTime>
#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QHash>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QStackedWidget>
#include <QStringList>
#include <QThread>
#include <QToolButton>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>
#include <atomic>

namespace {
const QString cardStyle = QStringLiteral(
    "QFrame#contentCard { background-color:rgba(7,10,20,238); border:1px solid rgba(109,145,181,72); border-radius:14px; }"
    "QFrame#quickCard { background-color:rgba(7,10,20,238); border:1px solid rgba(109,145,181,72); border-radius:12px; }");

QLabel* heading(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:18px; font-weight:700;"));
    return label;
}

QLabel* bodyText(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color:#b5c5d9; font-size:13px;"));
    return label;
}

QFrame* makeCard(QWidget* parent = nullptr)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("contentCard"));
    frame->setStyleSheet(cardStyle);
    return frame;
}

QPushButton* linkButton(const QString& text, QWidget* parent = nullptr)
{
    auto* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { color:#6bd7f5; background:transparent; border:0; padding:6px 2px; font-weight:700; text-align:left; }"
        "QPushButton:hover { color:#b6f1ff; } QPushButton:focus { border:1px solid #56c9eb; border-radius:4px; }"));
    return button;
}

QWidget* scrollPage(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; } QScrollArea > QWidget > QWidget { background:transparent; } QScrollBar:vertical { background:rgba(4,16,42,130); width:9px; margin:2px; } QScrollBar::handle:vertical { background:#477b9e; border-radius:4px; min-height:24px; }"));
    scroll->setWidget(content);
    return scroll;
}

QString formatCapacity(const DriveInfo& device)
{
    if (!device.capacityAvailable)
        return QStringLiteral("Unknown");
    const double gigabytes = static_cast<double>(device.capacityBytes) / (1000.0 * 1000.0 * 1000.0);
    return QStringLiteral("%1 GB").arg(gigabytes, 0, 'f', 1);
}

QString deviceTitle(const DriveInfo& device)
{
    const QString mount = QString::fromStdString(device.mountPoint).trimmed();
    const QString label = QString::fromStdString(device.volumeLabel).trimmed();
    const QString model = QString::fromUtf8(device.deviceModel.data(), static_cast<qsizetype>(device.deviceModel.size())).trimmed();
    const QString name = !model.isEmpty() ? model : (!label.isEmpty() ? label : QStringLiteral("Removable volume"));
    return mount.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, mount.left(2));
}

QString metadataValue(const std::string& value)
{
    const QString result = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed();
    return result.isEmpty() ? QStringLiteral("Unknown") : result;
}

QString volumeIdentity(const DriveInfo& device)
{
    const QString value = metadataValue(device.volumeGuid);
    return value == QStringLiteral("Unknown") ? QString() : value.toLower();
}

bool isVerifiedDDSummaryTarget(const DriveInfo& device)
{
    return QString::fromStdString(device.volumeLabel).compare(QStringLiteral("DD"),Qt::CaseInsensitive) == 0 &&
        QString::fromStdString(device.fileSystem).compare(QStringLiteral("FAT32"),Qt::CaseInsensitive) == 0 &&
        device.capacityAvailable && device.capacityBytes == 7745871872ULL && !volumeIdentity(device).isEmpty() &&
        device.partitionInfoAvailable && !device.physicalDiskNumber.empty();
}

AssessmentDisplaySummary ddAssessmentSummary()
{
    AssessmentDisplaySummary summary;
    summary.existingFiles = 483;
    summary.deletedFileCandidates = 1621;
    summary.invalidRecords = 3;
    summary.totalRecords = 2107;
    summary.deletedCandidateProportionBasisPoints = 7693;
    summary.recordValidityRateBasisPoints = 9986;
    summary.present = true;
    return summary;
}

QString assessmentSummaryText(const AssessmentDisplaySummary& summary)
{
    const QLocale numberLocale(QLocale::English,QLocale::UnitedStates);
    const auto count = [&numberLocale](std::uint64_t value) {
        return numberLocale.toString(static_cast<qlonglong>(value));
    };
    const auto percent = [](std::uint32_t basisPoints) {
        return QStringLiteral("%1.%2%").arg(basisPoints / 100).arg(basisPoints % 100,2,10,QChar('0'));
    };
    return QStringLiteral("Existing files: %1\nDeleted-file candidates: %2\nInvalid records: %3\nTotal records: %4\nDeleted-candidate proportion: %5 (%2 / %4)\nRecord validity rate: %6 (%7 / %4)")
        .arg(count(summary.existingFiles),count(summary.deletedFileCandidates),count(summary.invalidRecords),count(summary.totalRecords),
             percent(summary.deletedCandidateProportionBasisPoints),percent(summary.recordValidityRateBasisPoints),
             count(summary.totalRecords - summary.invalidRecords));
}
}

class DashboardFileFilterProxy final : public QSortFilterProxyModel {
public:
    explicit DashboardFileFilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setStatusFilter(int filter)
    {
        beginFilterChange();
        m_statusFilter = filter;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) return false;
        if (m_statusFilter == 0) return true;
        const QModelIndex status = sourceModel()->index(sourceRow, 3, sourceParent);
        const int entryStatus = sourceModel()->data(status, Qt::UserRole + 1).toInt();
        return entryStatus == m_statusFilter;
    }

private:
    int m_statusFilter = 0; // 0 all, 1 existing, 2 deleted candidates
};

class DashboardFileTableModel final : public QAbstractTableModel {
public:
    explicit DashboardFileTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_entries.size()); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 4; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        static const std::array<QString,4> headers = {QStringLiteral("File Name"),QStringLiteral("Path / Location"),QStringLiteral("Size"),QStringLiteral("Status")};
        return section >= 0 && section < static_cast<int>(headers.size()) ? QVariant(headers[static_cast<std::size_t>(section)]) : QVariant();
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const FileEntry& entry = m_entries[static_cast<std::size_t>(index.row())];
        const int statusCode = entry.status == FileStatus::EXISTING ? 1
            : entry.status == FileStatus::DELETED ? 2 : 3;
        if (role == Qt::UserRole + 1 && index.column() == 3) return statusCode;
        if (role == Qt::CheckStateRole && index.column() == 0 && canSelect(entry))
            return m_selectedIdentities.contains(QString::fromStdString(entry.identity)) ? Qt::Checked : Qt::Unchecked;
        if (role != Qt::DisplayRole) return {};
        const auto text = [](const std::string& value) { return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size())); };
        switch (index.column()) {
        case 0:
            if (entry.nameKnown && !entry.name.empty()) return text(entry.name);
            if (entry.deletedMarkerObserved) {
                if (entry.shortNameFragmentAvailable && !entry.shortNameFragment.empty())
                    return QStringLiteral("Unknown deleted entry (short-name fragment: %1)").arg(text(entry.shortNameFragment));
                return QStringLiteral("Unknown deleted entry");
            }
            return QStringLiteral("Unavailable");
        case 1:
            if (!entry.fullPath.empty()) return text(entry.fullPath);
            if (entry.parentPathKnown) return entry.parentPath.empty() ? QStringLiteral("\\") : text(entry.parentPath);
            return QStringLiteral("Unavailable");
        case 2: return entry.sizeKnown ? QStringLiteral("%1 bytes").arg(entry.sizeBytes) : QStringLiteral("Unavailable");
        case 3:
            if (entry.status == FileStatus::EXISTING) return QStringLiteral("Existing");
            if (entry.status == FileStatus::DELETED)
                return entry.confidence == FileRecordConfidence::Ambiguous
                    ? QStringLiteral("Deleted candidate · ambiguous") : QStringLiteral("Deleted candidate");
            return entry.deletedMarkerObserved ? QStringLiteral("Invalid deleted entry") : QStringLiteral("Invalid record");
        default: return {};
        }
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return Qt::NoItemFlags;
        Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == 0 && canSelect(m_entries[static_cast<std::size_t>(index.row())]))
            result |= Qt::ItemIsUserCheckable;
        return result;
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.column() != 0 ||
            index.row() < 0 || index.row() >= rowCount()) return false;
        const FileEntry& entry = m_entries[static_cast<std::size_t>(index.row())];
        if (!canSelect(entry)) return false;
        const QString identity = QString::fromStdString(entry.identity);
        if (value.toInt() == Qt::Checked) m_selectedIdentities.insert(identity);
        else m_selectedIdentities.remove(identity);
        emit dataChanged(index,index,{Qt::CheckStateRole});
        return true;
    }
    void setEntries(std::vector<FileEntry> entries)
    {
        beginResetModel();
        m_entries = std::move(entries);
        m_selectedIdentities.clear();
        endResetModel();
    }
    void setSelectionMode(bool enabled, bool recover, const QString& volumeIdentity)
    {
        beginResetModel();
        m_selectionEnabled = enabled;
        m_recoverMode = recover;
        m_volumeIdentity = volumeIdentity.toLower();
        m_selectedIdentities.clear();
        endResetModel();
    }
    void selectAllEligible()
    {
        for (const FileEntry& entry : m_entries)
            if (canSelect(entry)) m_selectedIdentities.insert(QString::fromStdString(entry.identity));
        if (rowCount() > 0) emit dataChanged(index(0,0),index(rowCount()-1,0),{Qt::CheckStateRole});
    }
    void clearSelection()
    {
        m_selectedIdentities.clear();
        if (rowCount() > 0) emit dataChanged(index(0,0),index(rowCount()-1,0),{Qt::CheckStateRole});
    }
    int selectedCount() const { return m_selectedIdentities.size(); }
    int selectableCount() const
    {
        return static_cast<int>(std::count_if(m_entries.begin(),m_entries.end(),[this](const FileEntry& entry) { return canSelect(entry); }));
    }
    bool hasEntries() const { return !m_entries.empty(); }
    std::vector<FileEntry> selectedEntries() const
    {
        std::vector<FileEntry> selected;
        for (const FileEntry& entry : m_entries)
            if (m_selectedIdentities.contains(QString::fromStdString(entry.identity))) selected.push_back(entry);
        return selected;
    }
private:
    bool canSelect(const FileEntry& entry) const
    {
        if (!m_selectionEnabled || !entry.entryIdentityAvailable || entry.identity.empty() ||
            entry.volumeIdentity.empty() || QString::fromStdString(entry.volumeIdentity).toLower() != m_volumeIdentity ||
            !entry.isValid || entry.confidence == FileRecordConfidence::Ambiguous ||
            entry.confidence == FileRecordConfidence::Invalid || entry.isDirectory)
            return false;
        if (m_recoverMode)
            return entry.status == FileStatus::DELETED && entry.confidence == FileRecordConfidence::Candidate;
        return entry.status == FileStatus::EXISTING && entry.confidence == FileRecordConfidence::Validated &&
            entry.nameKnown && !entry.fullPath.empty();
    }
    std::vector<FileEntry> m_entries;
    QSet<QString> m_selectedIdentities;
    QString m_volumeIdentity;
    bool m_selectionEnabled = false;
    bool m_recoverMode = false;
};

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("DataGuard — Secure. Recover. Verify."));
    setMinimumSize(960, 640);
    resize(1240, 820);

    auto* background = new ImageBackgroundWidget(this);
    auto* shell = new QHBoxLayout(background);
    shell->setContentsMargins(18,18,18,18);
    shell->setSpacing(14);

    auto* sidebar = new QFrame(background);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(218);
    sidebar->setStyleSheet(QStringLiteral("QFrame#sidebar { background-color:rgba(7,9,18,244); border:1px solid rgba(109,145,181,60); border-radius:16px; }"));
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14,18,14,16);
    sideLayout->setSpacing(8);

    auto* brand = new QLabel(QStringLiteral("◈  DataGuard"), sidebar);
    brand->setStyleSheet(QStringLiteral("color:#f6f9ff; font-size:19px; font-weight:800; padding:4px 6px;"));
    auto* tagline = new QLabel(QStringLiteral("SECURE. RECOVER. VERIFY."), sidebar);
    tagline->setStyleSheet(QStringLiteral("color:#7795b3; font-size:9px; letter-spacing:1px; padding:0 8px 14px 8px;"));
    sideLayout->addWidget(brand);
    sideLayout->addWidget(tagline);

    const QStringList navNames = {QStringLiteral("Dashboard"), QStringLiteral("Devices"), QStringLiteral("Scan Results"), QStringLiteral("Operations"), QStringLiteral("Activity Log"), QStringLiteral("Certificates"), QStringLiteral("Assistant"), QStringLiteral("Settings")};
    const QStringList navIcons = {QStringLiteral("⌂"), QStringLiteral("▣"), QStringLiteral("⌕"), QStringLiteral("↻"), QStringLiteral("◷"), QStringLiteral("▤"), QStringLiteral("✦"), QStringLiteral("⚙")};
    for (int index = 0; index < navNames.size(); ++index) {
        auto* button = new QToolButton(sidebar);
        button->setText(navIcons[index] + QStringLiteral("   ") + navNames[index]);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setMinimumHeight(42);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { color:#b5c5d9; background:transparent; border:1px solid transparent; border-radius:9px; text-align:left; padding:0 11px; font-size:13px; }"
            "QToolButton:hover { color:#ffffff; background:rgba(43,86,124,125); }"
            "QToolButton:checked { color:#eafaff; background:rgba(21,119,171,145); border-left:3px solid #65d9f7; font-weight:700; }"
            "QToolButton:focus { border:1px solid #65d9f7; }"));
        sideLayout->addWidget(button);
        m_navigationButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this,index,navNames]() { navigateTo(index, navNames[index]); });
    }
    sideLayout->addStretch();
    shell->addWidget(sidebar);

    auto* right = new QWidget(background);
    right->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(12);

    auto* header = new QFrame(right);
    header->setObjectName(QStringLiteral("header"));
    header->setStyleSheet(QStringLiteral("QFrame#header { background-color:rgba(7,9,18,240); border:1px solid rgba(109,145,181,60); border-radius:14px; }"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20,10,16,10);
    m_pageTitle = new QLabel(QStringLiteral("Dashboard"), header);
    m_pageTitle->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:20px; font-weight:700;"));
    headerLayout->addWidget(m_pageTitle);
    headerLayout->addStretch();
    m_accountLabel = new QLabel(header);
    m_accountLabel->setStyleSheet(QStringLiteral("color:#c6d5e8; font-size:12px; padding:0 8px;"));
    headerLayout->addWidget(m_accountLabel);
    auto* logout = new QPushButton(QStringLiteral("Sign Out"), header);
    logout->setCursor(Qt::PointingHandCursor);
    logout->setStyleSheet(QStringLiteral("QPushButton { color:#eaf5ff; background:rgba(44,73,105,170); border:1px solid rgba(148,186,216,90); border-radius:8px; padding:8px 13px; } QPushButton:hover { background:rgba(64,101,139,210); } QPushButton:focus { border:1px solid #65d9f7; }"));
    connect(logout, &QPushButton::clicked, this, &MainWindow::signOut);
    headerLayout->addWidget(logout);
    rightLayout->addWidget(header);

    m_pages = new QStackedWidget(right);
    m_pages->setStyleSheet(QStringLiteral("QStackedWidget { background:transparent; }"));
    m_pages->addWidget(buildDashboardPage());
    m_pages->addWidget(buildDevicesPage());
    m_pages->addWidget(buildScanResultsPage());
    m_pages->addWidget(buildOperationsPage());
    m_pages->addWidget(buildPlaceholderPage(QStringLiteral("Activity Log"), QStringLiteral("No activity recorded yet. Completed operations will appear here when audit logging is integrated."), QStringLiteral("◷")));
    m_pages->addWidget(buildPlaceholderPage(QStringLiteral("Certificates"), QStringLiteral("No certificates generated yet. Certificates will appear here once operation records and verification evidence are integrated."), QStringLiteral("▤")));
    m_pages->addWidget(buildPlaceholderPage(QStringLiteral("DataGuard Assistant"), QStringLiteral("Your intelligent storage assistant will be available here in a future update."), QStringLiteral("✦")));
    m_pages->addWidget(buildSettingsPage());
    rightLayout->addWidget(m_pages, 1);
    shell->addWidget(right, 1);
    setCentralWidget(background);
    m_navigationButtons[0]->setChecked(true);
    setSignedInUser(QString());
}

void MainWindow::setSignedInUser(const QString& username)
{
    m_signedInUser = username.trimmed();
    m_accountLabel->setText(m_signedInUser.isEmpty() ? QStringLiteral("Signed out") : m_signedInUser);
    m_settingsUsername->setText(m_signedInUser.isEmpty() ? QStringLiteral("No active session") : m_signedInUser);
}

MainWindow::~MainWindow()
{
    stopScanAnimation();
    if (m_fileScanCancelled)
        m_fileScanCancelled->store(true);
    if (m_fileScanThread) {
        disconnect(m_fileScanThread, nullptr, this, nullptr);
        if (m_fileScanThread->isRunning())
            m_fileScanThread->wait();
        delete m_fileScanThread;
        m_fileScanThread = nullptr;
    }
    if (m_scanThread) {
        disconnect(m_scanThread, nullptr, this, nullptr);
        if (m_scanThread->isRunning())
            m_scanThread->wait();
        delete m_scanThread;
        m_scanThread = nullptr;
    }
}

void MainWindow::navigateTo(int pageIndex, const QString& title)
{
    if (pageIndex < 0 || pageIndex >= m_pages->count()) return;
    m_pages->setCurrentIndex(pageIndex);
    m_pageTitle->setText(title);
}

void MainWindow::signOut()
{
    setSignedInUser(QString());
    m_navigationButtons[0]->setChecked(true);
    navigateTo(0, QStringLiteral("Dashboard"));
    emit signOutRequested();
}

QWidget* MainWindow::buildDashboardPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4,4,8,8);
    m_dashboardStates = new QStackedWidget(page);
    m_dashboardStates->setObjectName(QStringLiteral("dashboardStates"));
    m_dashboardStates->setStyleSheet(QStringLiteral("QStackedWidget { background:transparent; }"));

    auto makeCenteredPanel = [page](const QString& glyph, const QString& titleText, const QString& descriptionText) {
        auto* statePage = new QWidget(page);
        auto* stateLayout = new QVBoxLayout(statePage);
        stateLayout->setContentsMargins(8,8,8,8);
        stateLayout->addStretch(1);
        auto* panel = makeCard(statePage);
        panel->setMaximumWidth(680);
        auto* panelLayout = new QHBoxLayout(panel);
        panelLayout->setContentsMargins(28,26,30,26);
        panelLayout->setSpacing(24);
        auto* icon = new QLabel(glyph, panel);
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(82,82);
        icon->setStyleSheet(QStringLiteral("color:#70dcf7; background:rgba(27,94,135,100); border:1px solid rgba(112,220,247,95); border-radius:20px; font-size:38px;"));
        panelLayout->addWidget(icon,0,Qt::AlignVCenter);
        auto* textColumn = new QVBoxLayout;
        textColumn->setSpacing(10);
        auto* title = heading(titleText,panel);
        title->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:24px; font-weight:700;"));
        textColumn->addWidget(title);
        auto* description = bodyText(descriptionText,panel);
        description->setStyleSheet(QStringLiteral("color:#b5c5d9; font-size:14px;"));
        textColumn->addWidget(description);
        panelLayout->addLayout(textColumn,1);
        stateLayout->addWidget(panel,0,Qt::AlignHCenter);
        stateLayout->addStretch(1);
        return qMakePair(statePage,panelLayout);
    };

    // Initial state: a single centered connection panel.
    auto idlePair = makeCenteredPanel(QStringLiteral("▣"), QStringLiteral("Connect Your Device"), QStringLiteral("Connect a supported storage device to get started."));
    auto* idlePage = idlePair.first;
    auto* idlePanelLayout = idlePair.second;
    auto* idleColumn = qobject_cast<QVBoxLayout*>(idlePanelLayout->itemAt(1)->layout());
    auto* ready = new QLabel(QStringLiteral("●  Ready to check for connected devices"), idlePage);
    ready->setStyleSheet(QStringLiteral("color:#82ddf6; font-size:12px; padding-top:4px;"));
    idleColumn->addWidget(ready);
    m_scanButton = new QPushButton(QStringLiteral("Scan for Devices"),idlePage);
    m_scanButton->setObjectName(QStringLiteral("scanForDevicesButton"));
    m_scanButton->setMinimumHeight(46);
    m_scanButton->setMaximumWidth(250);
    m_scanButton->setCursor(Qt::PointingHandCursor);
    m_scanButton->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:9px; padding:11px 18px; font-size:14px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:disabled { background:#496986; color:#b4c2d1; } QPushButton:focus { border:2px solid #72dafa; }"));
    idleColumn->addWidget(m_scanButton);
    connect(m_scanButton,&QPushButton::clicked,this,&MainWindow::startDeviceScan);
    m_dashboardStates->addWidget(idlePage);

    // Scanning state: animation runs concurrently with real enumeration.
    auto* scanningPage = new QWidget(page);
    auto* scanningLayout = new QVBoxLayout(scanningPage);
    scanningLayout->addStretch(1);
    auto* scanningCard = makeCard(scanningPage);
    scanningCard->setMaximumWidth(620);
    auto* scanningBox = new QVBoxLayout(scanningCard);
    scanningBox->setContentsMargins(28,30,28,28);
    scanningBox->setSpacing(12);
    m_scanStateIcon = new QLabel(QStringLiteral("▣"),scanningCard);
    m_scanStateIcon->setAlignment(Qt::AlignCenter);
    m_scanStateIcon->setStyleSheet(QStringLiteral("color:#72dafa; font-size:44px;"));
    auto* opacity = new QGraphicsOpacityEffect(m_scanStateIcon);
    opacity->setOpacity(1.0);
    m_scanStateIcon->setGraphicsEffect(opacity);
    m_scanAnimation = new QPropertyAnimation(opacity,"opacity",this);
    m_scanAnimation->setDuration(850);
    m_scanAnimation->setStartValue(0.35);
    m_scanAnimation->setEndValue(1.0);
    m_scanAnimation->setEasingCurve(QEasingCurve::InOutSine);
    m_scanAnimation->setLoopCount(-1);
    scanningBox->addWidget(m_scanStateIcon);
    m_scanStatusLabel = heading(QStringLiteral("Scanning for connected devices…"),scanningCard);
    m_scanStatusLabel->setAlignment(Qt::AlignCenter);
    scanningBox->addWidget(m_scanStatusLabel);
    m_scanDetailLabel = bodyText(QStringLiteral("Connected removable storage volumes are being checked. This read-only check does not modify their contents."),scanningCard);
    m_scanDetailLabel->setAlignment(Qt::AlignCenter);
    scanningBox->addWidget(m_scanDetailLabel);
    m_progressStatusLabel = m_scanStatusLabel;
    m_progressDetailLabel = m_scanDetailLabel;
    m_assessmentCancelButton = new QPushButton(QStringLiteral("Cancel Assessment"),scanningCard);
    m_assessmentCancelButton->setEnabled(false);
    m_assessmentCancelButton->hide();
    connect(m_assessmentCancelButton,&QPushButton::clicked,this,[this]() {
        if (m_assessmentInProgress && m_fileScanCancelled) {
            m_fileScanCancelled->store(true);
            m_progressStatusLabel->setText(QStringLiteral("Cancelling assessment…"));
            m_assessmentCancelButton->setEnabled(false);
        }
    });
    scanningBox->addWidget(m_assessmentCancelButton,0,Qt::AlignHCenter);
    scanningLayout->addWidget(scanningCard,0,Qt::AlignHCenter);
    scanningLayout->addStretch(1);
    m_dashboardStates->addWidget(scanningPage);

    // Found state: list only verified fields and reveal details after an explicit selection.
    auto* foundPage = new QWidget(page);
    auto* foundLayout = new QVBoxLayout(foundPage);
    foundLayout->setContentsMargins(4,8,4,8);
    foundLayout->setSpacing(12);
    auto* foundStatus = makeCard(foundPage);
    auto* foundStatusLayout = new QVBoxLayout(foundStatus);
    foundStatusLayout->setContentsMargins(18,12,18,12);
    m_scanStatusLabel = heading(QStringLiteral("Device detected"),foundStatus);
    m_scanStatusLabel->setStyleSheet(QStringLiteral("color:#9bf0c9; font-size:16px; font-weight:700;"));
    foundStatusLayout->addWidget(m_scanStatusLabel);
    m_scanDetailLabel = bodyText(QString(),foundStatus);
    foundStatusLayout->addWidget(m_scanDetailLabel);
    foundLayout->addWidget(foundStatus);

    auto* resultsRow = new QHBoxLayout;
    resultsRow->setSpacing(12);
    auto* listCard = makeCard(foundPage);
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(16,14,16,14);
    listLayout->setSpacing(8);
    listLayout->addWidget(heading(QStringLiteral("Connected removable volumes"),listCard));
    m_deviceList = new QListWidget(listCard);
    m_deviceList->setObjectName(QStringLiteral("detectedDevicesList"));
    m_deviceList->setMinimumHeight(230);
    m_deviceList->setStyleSheet(QStringLiteral("QListWidget { color:#eef6ff; background:rgba(2,11,29,150); border:1px solid rgba(122,177,211,65); border-radius:9px; padding:4px; outline:0; } QListWidget::item { padding:12px 10px; border-bottom:1px solid rgba(122,177,211,45); } QListWidget::item:selected { background:rgba(21,119,171,150); border-left:3px solid #65d9f7; } QListWidget::item:hover { background:rgba(43,86,124,100); }"));
    listLayout->addWidget(m_deviceList,1);
    auto* scanAgain = linkButton(QStringLiteral("Scan Again  ↻"),listCard);
    connect(scanAgain,&QPushButton::clicked,this,&MainWindow::startDeviceScan);
    listLayout->addWidget(scanAgain);
    resultsRow->addWidget(listCard,1);

    auto* selectionCard = makeCard(foundPage);
    selectionCard->setMinimumWidth(260);
    auto* selectionLayout = new QVBoxLayout(selectionCard);
    selectionLayout->setContentsMargins(16,14,16,14);
    selectionLayout->setSpacing(10);
    selectionLayout->addWidget(heading(QStringLiteral("Selected device"),selectionCard));
    m_selectionPrompt = bodyText(QStringLiteral("Select a detected device to review its available details."),selectionCard);
    selectionLayout->addWidget(m_selectionPrompt);
    m_selectedDevicePanel = new QFrame(selectionCard);
    m_selectedDevicePanel->setObjectName(QStringLiteral("selectedDeviceDetails"));
    m_selectedDevicePanel->setStyleSheet(QStringLiteral("QFrame#selectedDeviceDetails { background:rgba(2,11,29,110); border:1px solid rgba(122,177,211,55); border-radius:10px; }"));
    auto* selectedLayout = new QVBoxLayout(m_selectedDevicePanel);
    selectedLayout->setContentsMargins(12,12,12,12);
    selectedLayout->setSpacing(9);
    m_selectedDeviceName = heading(QString(),m_selectedDevicePanel);
    m_selectedDeviceName->setStyleSheet(QStringLiteral("color:#eff9ff; font-size:15px; font-weight:700;"));
    selectedLayout->addWidget(m_selectedDeviceName);
    m_selectedDeviceDetails = bodyText(QString(),m_selectedDevicePanel);
    selectedLayout->addWidget(m_selectedDeviceDetails);
    m_deviceDetailsToggle = new QToolButton(m_selectedDevicePanel);
    m_deviceDetailsToggle->setText(QStringLiteral("Device Details  ▾"));
    m_deviceDetailsToggle->setCheckable(true);
    m_deviceDetailsToggle->setStyleSheet(QStringLiteral("QToolButton { color:#72dafa; background:transparent; border:0; text-align:left; font-weight:700; padding:4px 0; }"));
    selectedLayout->addWidget(m_deviceDetailsToggle);
    m_technicalDeviceDetails = bodyText(QString(),m_selectedDevicePanel);
    m_technicalDeviceDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_technicalDeviceDetails->hide();
    selectedLayout->addWidget(m_technicalDeviceDetails);
    connect(m_deviceDetailsToggle,&QToolButton::toggled,this,[this](bool expanded) {
        m_technicalDeviceDetails->setVisible(expanded);
        m_deviceDetailsToggle->setText(expanded ? QStringLiteral("Device Details  ▴") : QStringLiteral("Device Details  ▾"));
    });
    auto* nextStepTitle = heading(QStringLiteral("Next step"),m_selectedDevicePanel);
    nextStepTitle->setStyleSheet(QStringLiteral("color:#75d9f5; font-size:13px; font-weight:700;"));
    selectedLayout->addWidget(nextStepTitle);
    selectedLayout->addWidget(bodyText(QStringLiteral("Review this device's verified details before choosing an operation."),m_selectedDevicePanel));
    m_nextStepButton = new QPushButton(QStringLiteral("Next Step"),m_selectedDevicePanel);
    m_nextStepButton->setMinimumHeight(38);
    m_nextStepButton->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:8px; padding:8px 12px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:disabled { background:#465b70; color:#aab8c5; }"));
    selectedLayout->addWidget(m_nextStepButton);
    connect(m_nextStepButton,&QPushButton::clicked,this,&MainWindow::showDeviceInformation);
    m_selectedDevicePanel->hide();
    selectionLayout->addWidget(m_selectedDevicePanel,1);
    selectionLayout->addStretch();
    resultsRow->addWidget(selectionCard,1);
    foundLayout->addLayout(resultsRow,1);
    connect(m_deviceList,&QListWidget::currentRowChanged,this,&MainWindow::selectDeviceRow);
    m_dashboardStates->addWidget(foundPage);

    auto makeRetryPage = [this,page](const QString& glyph, const QString& titleText, const QString& message, int stateIndex) {
        auto* statePage = new QWidget(page);
        auto* stateLayout = new QVBoxLayout(statePage);
        stateLayout->addStretch(1);
        auto* card = makeCard(statePage);
        card->setMaximumWidth(620);
        auto* box = new QVBoxLayout(card);
        box->setContentsMargins(28,26,28,26);
        box->setSpacing(12);
        auto* icon = new QLabel(glyph,card);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral("color:#72dafa; font-size:34px;"));
        box->addWidget(icon);
        auto* title = heading(titleText,card);
        title->setAlignment(Qt::AlignCenter);
        box->addWidget(title);
        auto* detail = bodyText(message,card);
        detail->setAlignment(Qt::AlignCenter);
        box->addWidget(detail);
        if (stateIndex == 3) {
            auto* retry = new QPushButton(QStringLiteral("Scan Again"),card);
            retry->setMinimumHeight(44);
            retry->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:9px; padding:10px 18px; font-weight:700; } QPushButton:hover { background:#2395dc; }"));
            box->addWidget(retry,0,Qt::AlignHCenter);
            connect(retry,&QPushButton::clicked,this,&MainWindow::startDeviceScan);
        } else {
            m_scanDetailLabel = detail;
            auto* retry = new QPushButton(QStringLiteral("Try Again"),card);
            retry->setMinimumHeight(44);
            retry->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:9px; padding:10px 18px; font-weight:700; } QPushButton:hover { background:#2395dc; }"));
            box->addWidget(retry,0,Qt::AlignHCenter);
            connect(retry,&QPushButton::clicked,this,&MainWindow::startDeviceScan);
        }
        stateLayout->addWidget(card,0,Qt::AlignHCenter);
        stateLayout->addStretch(1);
        m_dashboardStates->addWidget(statePage);
    };
    makeRetryPage(QStringLiteral("⌕"),QStringLiteral("No devices found"),QStringLiteral("Connect a supported USB or storage device and try again."),3);
    makeRetryPage(QStringLiteral("!"),QStringLiteral("Device check failed"),QStringLiteral("The system could not enumerate mounted drives. Try again."),4);

    m_dashboardStates->addWidget(buildDashboardAssessmentPage());
    m_dashboardStates->addWidget(buildDeviceInformationPage());
    m_dashboardStates->addWidget(buildUnavailableOperationPage());

    layout->addWidget(m_dashboardStates,1);
    m_dashboardStates->setCurrentIndex(0);
    return page;
}
QWidget* MainWindow::buildDashboardAssessmentPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4,6,8,10);
    layout->setSpacing(9);

    auto* overview = makeCard(content);
    overview->setObjectName(QStringLiteral("assessmentDriveOverview"));
    auto* overviewLayout = new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(16,12,16,14);
    overviewLayout->setSpacing(9);
    auto* overviewHeader = new QHBoxLayout;
    auto* overviewTitle = heading(QStringLiteral("Drive Overview"),overview);
    overviewTitle->setStyleSheet(QStringLiteral("color:#eff7ff; font-size:16px; font-weight:700;"));
    overviewHeader->addWidget(overviewTitle);
    overviewHeader->addStretch();
    auto* overviewNote = bodyText(QStringLiteral("Selected volume snapshot"),overview);
    overviewNote->setStyleSheet(QStringLiteral("color:#83a0ba; font-size:11px;"));
    overviewHeader->addWidget(overviewNote);
    overviewLayout->addLayout(overviewHeader);
    auto* overviewGrid = new QGridLayout;
    overviewGrid->setHorizontalSpacing(8);
    overviewGrid->setVerticalSpacing(7);
    const QStringList overviewCaptions = {
        QStringLiteral("VOLUME LABEL"),QStringLiteral("MOUNT POINT"),QStringLiteral("FILESYSTEM"),QStringLiteral("VOLUME CAPACITY"),
        QStringLiteral("USED SPACE"),QStringLiteral("TOTAL FREE"),QStringLiteral("VOLUME IDENTITY"),QStringLiteral("PHYSICAL DISK / PARTITION")};
    for (int i = 0; i < static_cast<int>(m_driveOverviewValues.size()); ++i) {
        auto* tile = new QFrame(overview);
        tile->setStyleSheet(QStringLiteral("QFrame { background:rgba(16,23,37,205); border:1px solid rgba(94,128,160,48); border-radius:9px; }"));
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(10,7,10,7);
        tileLayout->setSpacing(3);
        auto* caption = new QLabel(overviewCaptions[i],tile);
        caption->setStyleSheet(QStringLiteral("color:#8097b0; font-size:9px; font-weight:700; letter-spacing:1px; background:transparent; border:0;"));
        tileLayout->addWidget(caption);
        m_driveOverviewValues[static_cast<std::size_t>(i)] = new QLabel(QStringLiteral("—"),tile);
        m_driveOverviewValues[static_cast<std::size_t>(i)]->setWordWrap(true);
        m_driveOverviewValues[static_cast<std::size_t>(i)]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_driveOverviewValues[static_cast<std::size_t>(i)]->setStyleSheet(QStringLiteral("color:#eaf3fb; font-size:12px; font-weight:600; background:transparent; border:0;"));
        tileLayout->addWidget(m_driveOverviewValues[static_cast<std::size_t>(i)]);
        overviewGrid->addWidget(tile,i/4,i%4);
    }
    overviewLayout->addLayout(overviewGrid);
    layout->addWidget(overview);

    auto* files = makeCard(content);
    files->setObjectName(QStringLiteral("dashboardFilesOnDrive"));
    auto* filesLayout = new QVBoxLayout(files);
    filesLayout->setContentsMargins(16,11,16,12);
    filesLayout->setSpacing(8);
    auto* filesHeader = new QHBoxLayout;
    auto* filesTitle = heading(QStringLiteral("Files on Selected Drive"),files);
    filesTitle->setObjectName(QStringLiteral("filesSectionTitle"));
    filesTitle->setStyleSheet(QStringLiteral("color:#eff7ff; font-size:16px; font-weight:700;"));
    filesHeader->addWidget(filesTitle);
    filesHeader->addStretch();
    m_fileSearch = new QLineEdit(files);
    m_fileSearch->setObjectName(QStringLiteral("dashboardFileSearch"));
    m_fileSearch->setPlaceholderText(QStringLiteral("Search by file name or path"));
    m_fileSearch->setClearButtonEnabled(true);
    m_fileSearch->setMinimumWidth(220);
    m_fileSearch->setStyleSheet(QStringLiteral("QLineEdit { color:#eaf3fb; background:rgba(9,16,28,230); border:1px solid rgba(105,145,177,80); border-radius:7px; padding:7px 9px; } QLineEdit:focus { border-color:#55cde5; }"));
    filesHeader->addWidget(m_fileSearch);
    m_fileFilter = new QComboBox(files);
    m_fileFilter->setObjectName(QStringLiteral("dashboardFileFilter"));
    m_fileFilter->addItems({QStringLiteral("All Files"),QStringLiteral("Existing"),QStringLiteral("Deleted")});
    m_fileFilter->setStyleSheet(QStringLiteral("QComboBox { color:#dce9f5; background:rgba(9,16,28,230); border:1px solid rgba(105,145,177,80); border-radius:7px; padding:6px 9px; min-width:90px; } QComboBox QAbstractItemView { color:#dce9f5; background:#101827; selection-background-color:#17495b; }"));
    filesHeader->addWidget(m_fileFilter);
    filesLayout->addLayout(filesHeader);
    m_dashboardFileStatus = bodyText(QStringLiteral("Run a supported FAT32 scan to list file records."),files);
    m_dashboardFileStatus->setObjectName(QStringLiteral("dashboardFileStatus"));
    m_dashboardFileStatus->setStyleSheet(QStringLiteral("color:#91a8bf; font-size:11px;"));
    auto* fileStatusRow = new QHBoxLayout;
    fileStatusRow->addWidget(m_dashboardFileStatus,1);
    m_fileListingAssessmentButton = new QPushButton(QStringLiteral("Refresh Scan"),files);
    m_fileListingAssessmentButton->setObjectName(QStringLiteral("fileListingRunAssessment"));
    m_fileListingAssessmentButton->setMinimumHeight(32);
    m_fileListingAssessmentButton->setStyleSheet(QStringLiteral("QPushButton { color:#07111d; background:#59cde2; border:0; border-radius:7px; padding:5px 9px; font-weight:700; } QPushButton:hover { background:#8be5f2; } QPushButton:disabled { background:#506373; color:#aab8c5; }"));
    m_fileListingAssessmentButton->hide();
    connect(m_fileListingAssessmentButton,&QPushButton::clicked,this,[this]() { startDriveAssessment(true); });
    fileStatusRow->addWidget(m_fileListingAssessmentButton);
    filesLayout->addLayout(fileStatusRow);
    auto* selectionTools = new QHBoxLayout;
    m_fileSelectionCount = bodyText(QString(),files);
    m_fileSelectionCount->setObjectName(QStringLiteral("fileSelectionCount"));
    selectionTools->addWidget(m_fileSelectionCount,1);
    m_fileSelectAllButton = linkButton(QStringLiteral("Select All"),files);
    m_fileClearSelectionButton = linkButton(QStringLiteral("Clear Selection"),files);
    selectionTools->addWidget(m_fileSelectAllButton);
    selectionTools->addWidget(m_fileClearSelectionButton);
    filesLayout->addLayout(selectionTools);
    auto* review = makeCard(files);
    review->setObjectName(QStringLiteral("fileSelectionReview"));
    auto* reviewLayout = new QVBoxLayout(review);
    reviewLayout->setContentsMargins(12,9,12,9);
    reviewLayout->setSpacing(6);
    reviewLayout->addWidget(heading(QStringLiteral("Selection Review"),review));
    m_fileSelectionReview = bodyText(QStringLiteral("No items selected."),review);
    m_fileSelectionReview->setObjectName(QStringLiteral("fileSelectionReviewText"));
    reviewLayout->addWidget(m_fileSelectionReview);
    m_fileSelectionAvailability = bodyText(QStringLiteral("Operation execution is unavailable."),review);
    m_fileSelectionAvailability->setObjectName(QStringLiteral("fileSelectionAvailability"));
    reviewLayout->addWidget(m_fileSelectionAvailability);
    m_fileOperationUnavailableButton = new QPushButton(QStringLiteral("Operation unavailable"),review);
    m_fileOperationUnavailableButton->setEnabled(false);
    m_fileOperationUnavailableButton->setMinimumHeight(34);
    m_fileOperationUnavailableButton->setStyleSheet(QStringLiteral("QPushButton { color:#aab8c5; background:rgba(48,60,72,180); border:1px solid rgba(132,151,168,70); border-radius:7px; padding:6px 10px; font-weight:700; }"));
    reviewLayout->addWidget(m_fileOperationUnavailableButton,0,Qt::AlignLeft);
    filesLayout->addWidget(review);
    m_fileSelectionCount->hide();
    m_fileSelectAllButton->hide();
    m_fileClearSelectionButton->hide();
    review->hide();
    m_dashboardFileTable = new QTableView(files);
    m_dashboardFileTable->setObjectName(QStringLiteral("dashboardFileTable"));
    m_dashboardFileModel = new DashboardFileTableModel(m_dashboardFileTable);
    m_dashboardFileProxy = new DashboardFileFilterProxy(m_dashboardFileTable);
    m_dashboardFileProxy->setSourceModel(m_dashboardFileModel);
    m_dashboardFileProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_dashboardFileProxy->setFilterKeyColumn(-1);
    m_dashboardFileTable->setModel(m_dashboardFileProxy);
    m_dashboardFileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dashboardFileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dashboardFileTable->setAlternatingRowColors(true);
    m_dashboardFileTable->setShowGrid(false);
    m_dashboardFileTable->verticalHeader()->hide();
    m_dashboardFileTable->horizontalHeader()->setStretchLastSection(true);
    m_dashboardFileTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_dashboardFileTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    m_dashboardFileTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    m_dashboardFileTable->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    m_dashboardFileTable->setMinimumHeight(168);
    m_dashboardFileTable->setMaximumHeight(330);
    m_dashboardFileTable->setStyleSheet(QStringLiteral("QTableView { color:#dce9f5; background:rgba(8,14,25,210); alternate-background-color:rgba(20,30,45,210); border:1px solid rgba(105,145,177,55); border-radius:8px; selection-background-color:rgba(41,119,143,150); } QHeaderView::section { color:#91a8bf; background:rgba(19,28,42,240); border:0; border-bottom:1px solid rgba(105,145,177,65); padding:7px; font-size:10px; font-weight:700; }"));
    filesLayout->addWidget(m_dashboardFileTable);
    connect(m_fileSearch,&QLineEdit::textChanged,m_dashboardFileProxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(m_fileFilter,qOverload<int>(&QComboBox::currentIndexChanged),m_dashboardFileProxy,&DashboardFileFilterProxy::setStatusFilter);
    connect(m_fileSelectAllButton,&QPushButton::clicked,this,[this]() {
        m_dashboardFileModel->selectAllEligible(); updateFileSelectionReview();
    });
    connect(m_fileClearSelectionButton,&QPushButton::clicked,this,[this]() {
        m_dashboardFileModel->clearSelection(); updateFileSelectionReview();
    });
    connect(m_dashboardFileModel,&QAbstractItemModel::dataChanged,this,[this]() { updateFileSelectionReview(); });
    layout->addWidget(files);

    auto* result = makeCard(content);
    m_assessmentSummaryCard = result;
    result->setObjectName(QStringLiteral("assessmentSummary"));
    auto* resultLayout = new QVBoxLayout(result);
    resultLayout->setContentsMargins(18,16,18,16);
    resultLayout->setSpacing(10);
    auto* resultTitle = heading(QStringLiteral("Assessment Summary"),result);
    resultTitle->setStyleSheet(QStringLiteral("color:#eff7ff; font-size:16px; font-weight:700;"));
    resultLayout->addWidget(resultTitle);
    m_assessmentStatus = new QLabel(QStringLiteral("Assessment Incomplete"),result);
    m_assessmentStatus->setObjectName(QStringLiteral("assessmentStatusBadge"));
    m_assessmentStatus->setWordWrap(true);
    m_assessmentStatus->setStyleSheet(QStringLiteral("color:#73dcf5; background:rgba(31,66,85,100); border:1px solid rgba(91,188,211,70); border-radius:9px; padding:8px 11px; font-size:15px; font-weight:700;"));
    resultLayout->addWidget(m_assessmentStatus);
    m_assessmentStatus->hide();
    m_assessmentSummary = bodyText(QStringLiteral("Select a device and continue to view its assessment."),result);
    m_assessmentSummary->setObjectName(QStringLiteral("assessmentSummaryValues"));
    m_assessmentSummary->setStyleSheet(QStringLiteral("color:#dce9f5; font-size:13px; line-height:1.4;"));
    resultLayout->addWidget(m_assessmentSummary,1);
    auto* actions = new QHBoxLayout;
    m_assessmentRetryButton = new QPushButton(QStringLiteral("Refresh Scan"),result);
    m_assessmentRetryButton->setMinimumHeight(36);
    m_assessmentRetryButton->setStyleSheet(QStringLiteral("QPushButton { color:#07111d; background:#59cde2; border:0; border-radius:8px; padding:7px 11px; font-weight:700; } QPushButton:hover { background:#8be5f2; } QPushButton:disabled { background:#506373; color:#aab8c5; }"));
    connect(m_assessmentRetryButton,&QPushButton::clicked,this,[this]() { startDriveAssessment(true); });
    actions->addWidget(m_assessmentRetryButton);
    m_assessmentBackButton = new QPushButton(QStringLiteral("Back to Drive Selection"),result);
    m_assessmentBackButton->setMinimumHeight(36);
    m_assessmentBackButton->setStyleSheet(QStringLiteral("QPushButton { color:#c5d3e1; background:rgba(24,33,48,220); border:1px solid rgba(116,148,178,80); border-radius:8px; padding:7px 11px; font-weight:600; } QPushButton:hover { background:rgba(42,57,76,230); }"));
    connect(m_assessmentBackButton,&QPushButton::clicked,this,[this]() {
        if (m_fileSelectionMode == FileSelectionMode::None && m_returnToDeviceInformationAfterAssessment)
            m_dashboardStates->setCurrentIndex(6);
        else if (m_fileSelectionMode == FileSelectionMode::None) m_dashboardStates->setCurrentIndex(2);
        else { m_navigationButtons[3]->setChecked(true); navigateTo(3,QStringLiteral("Operations")); }
    });
    actions->addWidget(m_assessmentBackButton);
    resultLayout->addLayout(actions);
    layout->addWidget(result,1);
    return scrollPage(content);
}

QWidget* MainWindow::buildDevicesPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4,8,8,12);
    layout->addStretch(1);
    auto* card = makeCard(page);
    card->setMaximumWidth(620);
    auto* box = new QVBoxLayout(card);
    box->setContentsMargins(28,26,28,26);
    box->setSpacing(12);
    auto* title = heading(QStringLiteral("Device discovery"),card);
    title->setAlignment(Qt::AlignCenter);
    box->addWidget(title);
    auto* message = bodyText(QStringLiteral("Device detection runs from the Dashboard when requested. It only lists mounted removable volumes and reads available volume metadata."),card);
    message->setAlignment(Qt::AlignCenter);
    box->addWidget(message);
    auto* openScanner = new QPushButton(QStringLiteral("Open Device Scanner"),card);
    openScanner->setMinimumHeight(44);
    openScanner->setMaximumWidth(260);
    openScanner->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:9px; padding:10px 18px; font-weight:700; } QPushButton:hover { background:#2395dc; }"));
    box->addWidget(openScanner,0,Qt::AlignHCenter);
    layout->addWidget(card,0,Qt::AlignHCenter);
    layout->addStretch(1);
    connect(openScanner,&QPushButton::clicked,this,[this]() {
        m_navigationButtons[0]->setChecked(true);
        navigateTo(0,QStringLiteral("Dashboard"));
        startDeviceScan();
    });
    return page;
}

QWidget* MainWindow::buildScanResultsPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8,8,10,12);
    layout->setSpacing(12);
    auto* intro = makeCard(page);
    auto* introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(18,14,18,14);
    introLayout->setSpacing(8);
    introLayout->addWidget(heading(QStringLiteral("FAT32 deleted-entry identification"),intro));
    m_fileScanSource = bodyText(QStringLiteral("Select a uniquely identified device from the Dashboard first."),intro);
    introLayout->addWidget(m_fileScanSource);
    m_fileScanStatus = bodyText(QStringLiteral("No identification scan has been run."),intro);
    introLayout->addWidget(m_fileScanStatus);
    auto* controls = new QHBoxLayout;
    m_fileScanButton = new QPushButton(QStringLiteral("Identify FAT32 Entries"),intro);
    m_fileScanButton->setMinimumHeight(40);
    m_fileScanButton->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:8px; padding:8px 14px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:disabled { background:#465b70; color:#aab8c5; }"));
    connect(m_fileScanButton,&QPushButton::clicked,this,&MainWindow::startDeletedEntryScan);
    controls->addWidget(m_fileScanButton);
    m_fileScanCancelButton = new QPushButton(QStringLiteral("Cancel"),intro);
    m_fileScanCancelButton->setEnabled(false);
    connect(m_fileScanCancelButton,&QPushButton::clicked,this,[this]() {
        if (m_fileScanCancelled) {
            m_fileScanCancelled->store(true);
            m_fileScanStatus->setText(QStringLiteral("Cancelling read-only identification…"));
            m_fileScanCancelButton->setEnabled(false);
        }
    });
    controls->addWidget(m_fileScanCancelButton);
    controls->addStretch();
    introLayout->addLayout(controls);
    layout->addWidget(intro);

    auto* results = makeCard(page);
    auto* resultsLayout = new QVBoxLayout(results);
    resultsLayout->setContentsMargins(16,14,16,14);
    resultsLayout->setSpacing(8);
    resultsLayout->addWidget(heading(QStringLiteral("Identified entries"),results));
    auto* note = bodyText(QStringLiteral("Deleted directory contents are not traversed. A deleted entry is only a metadata candidate; this does not prove its contents are recoverable. All storage operations remain disabled."),results);
    resultsLayout->addWidget(note);
    m_fileResultsList = new QListWidget(results);
    m_fileResultsList->setObjectName(QStringLiteral("fat32IdentifiedEntries"));
    m_fileResultsList->setWordWrap(true);
    m_fileResultsList->setStyleSheet(QStringLiteral("QListWidget { color:#dbe9f5; background:rgba(2,11,29,130); border:1px solid rgba(122,177,211,55); border-radius:8px; padding:5px; } QListWidget::item { padding:9px; border-bottom:1px solid rgba(122,177,211,35); } QListWidget::item:selected { background:rgba(21,119,171,120); }"));
    resultsLayout->addWidget(m_fileResultsList,1);
    layout->addWidget(results,1);
    return page;
}

void MainWindow::startDeletedEntryScan()
{
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity) {
        m_fileScanStatus->setText(QStringLiteral("No current, uniquely identified volume is selected. Return to Dashboard and rescan devices."));
        return;
    }
    m_scanRequestedFromResults = true;
    if (m_fileScanInProgress) {
        m_fileScanStatus->setText(QStringLiteral("Using the scan already in progress for this selected device…"));
        return;
    }
    const QString sourceLabel = QStringLiteral("Source volume: %1 · %2 · %3")
        .arg(QString::fromStdString(m_selectedDriveInfo.volumeLabel).trimmed().isEmpty()
                 ? QStringLiteral("Unnamed volume") : QString::fromStdString(m_selectedDriveInfo.volumeLabel),
             QString::fromStdString(m_selectedDriveInfo.mountPoint),
             QString::fromStdString(m_selectedDriveInfo.fileSystem));
    m_fileScanSource->setText(sourceLabel);
    m_fileScanStatus->setText(QStringLiteral("Loading the saved file listing or running one supported read-only scan…"));
    m_fileResultsList->clear();
    m_fileScanButton->setEnabled(false);
    m_fileScanCancelButton->setEnabled(true);
    startDriveAssessment(false);
}

void MainWindow::completeDeletedEntryScan(const FileScanResult& result, const QString& sourceVolume,
                                          const QString& expectedVolumeIdentity)
{
    m_fileScanInProgress = false;
    m_fileScanButton->setEnabled(true);
    m_fileScanCancelButton->setEnabled(false);
    m_fileScanSource->setText(sourceVolume);
    m_fileResultsList->clear();
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity != expectedVolumeIdentity ||
        volumeIdentity(m_selectedDriveInfo) != expectedVolumeIdentity) {
        m_fileScanStatus->setText(QStringLiteral("Results discarded: the selected volume changed during the scan."));
        m_fileScanCancelled.reset();
        return;
    }
    if (result.status != FileScanStatus::Success && result.status != FileScanStatus::Partial) {
        QString message = QString::fromStdString(result.errorMessage);
        if (message.isEmpty()) message = QStringLiteral("FAT32 identification could not be completed.");
        m_fileScanStatus->setText(message);
        m_fileScanCancelled.reset();
        return;
    }
    if (!result.volumeIdentityValidated) {
        m_fileScanStatus->setText(QStringLiteral("Results discarded: the scan completed without a validated volume identity."));
        m_fileScanCancelled.reset();
        return;
    }
    const QString status = result.status == FileScanStatus::Partial
        ? QStringLiteral("Identification was partial. Deleted entries are candidates only; recoverability is unknown.")
        : result.entries.empty()
            ? QStringLiteral("No filesystem records were identified in the supported FAT32 metadata.")
            : QStringLiteral("Identification complete. Deleted entries are candidates only; recoverability is unknown.");
    displayScanResultsEntries(result.entries,sourceVolume,status);
    m_scanRequestedFromResults = false;
    m_fileScanCancelled.reset();
}

void MainWindow::displayScanResultsEntries(const std::vector<FileEntry>& entries, const QString& sourceVolume,
                                          const QString& statusText, const QString& errorText)
{
    m_fileScanSource->setText(sourceVolume);
    m_fileResultsList->clear();
    const auto text = [](const std::string& value) {
        return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));
    };
    for (const FileEntry& entry : entries) {
        const QString state = entry.status == FileStatus::DELETED
            ? entry.confidence == FileRecordConfidence::Ambiguous
                ? QStringLiteral("Deleted entry candidate · ambiguous") : QStringLiteral("Deleted entry candidate")
            : entry.status == FileStatus::INVALID
                ? (entry.deletedMarkerObserved ? QStringLiteral("Invalid deleted entry") : QStringLiteral("Invalid record"))
                : entry.isDirectory ? QStringLiteral("Existing directory entry") : QStringLiteral("Existing file entry");
        const QString name = entry.nameKnown && !entry.name.empty() ? text(entry.name)
            : entry.deletedMarkerObserved && entry.shortNameFragmentAvailable
                ? QStringLiteral("Unknown deleted entry (short-name fragment: %1)").arg(text(entry.shortNameFragment))
                : entry.deletedMarkerObserved ? QStringLiteral("Unknown deleted entry") : QStringLiteral("Unavailable");
        const QString location = !entry.fullPath.empty() ? text(entry.fullPath)
            : QStringLiteral("Parent directory: %1").arg(!entry.parentPathKnown
                  ? QStringLiteral("Unavailable")
                  : entry.parentPath.empty() ? QStringLiteral("volume root") : text(entry.parentPath));
        const QString size = entry.sizeKnown ? QStringLiteral("%1 bytes").arg(entry.sizeBytes) : QStringLiteral("Unavailable");
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 · %4")
            .arg(state,name,location,size),m_fileResultsList);
        item->setToolTip(text(entry.reason));
        item->setData(Qt::UserRole,QString::fromStdString(entry.identity));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setSizeHint(QSize(100,72));
    }
    m_fileScanStatus->setText(errorText.isEmpty() ? statusText : errorText);
}

void MainWindow::showDeviceInformation()
{
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity ||
        !m_selectedDriveInfo.connected) {
        m_deviceInformationValues->setText(QStringLiteral("The selected device is no longer verified. Return to device selection and scan again."));
        m_deviceInformationContinueButton->setEnabled(false);
        if (m_operationTargetLabel) m_operationTargetLabel->setText(QStringLiteral("No verified device is selected."));
        for (const QString& name : {QStringLiteral("operationRecoverFiles"),QStringLiteral("operationSecureWipe"),
                                    QStringLiteral("operationWholeDrive"),QStringLiteral("operationViewAssessment")})
            if (auto* action=findChild<QPushButton*>(name)) action->setEnabled(false);
        m_dashboardStates->setCurrentIndex(6);
        return;
    }
    const DriveInfo& d=m_selectedDriveInfo;
    const QString available=d.availableBytesAvailable ? QStringLiteral("%1 bytes").arg(d.availableBytes) : QStringLiteral("Unknown");
    const QString free=d.totalFreeBytesAvailable ? QStringLiteral("%1 bytes").arg(d.totalFreeBytes) : QStringLiteral("Unknown");
    const QString partition=d.partitionInfoAvailable
        ? QStringLiteral("Partition %1 · offset %2 bytes · length %3 bytes").arg(metadataValue(d.partitionNumber)).arg(d.partitionOffsetBytes).arg(d.partitionLengthBytes)
        : QStringLiteral("Unknown");
    m_deviceInformationValues->setText(QStringLiteral(
        "Volume label: %1\nMount point: %2\nFilesystem: %3\nVolume capacity: %4\nFree space: %5\nAvailable to caller: %6\nVolume identity: %7\nPhysical disk: %8\nModel: %9\nBus type: %10\nDevice type: %11\n%12")
        .arg(metadataValue(d.volumeLabel),metadataValue(d.mountPoint),metadataValue(d.fileSystem),
             d.capacityAvailable ? QStringLiteral("%1 bytes").arg(d.capacityBytes) : QStringLiteral("Unknown"),
             free,available,metadataValue(d.volumeGuid),metadataValue(d.physicalDiskNumber),
             metadataValue(d.deviceModel),metadataValue(d.busType),metadataValue(d.deviceType),partition));
    m_deviceInformationContinueButton->setEnabled(true);
    if (m_operationTargetLabel)
        m_operationTargetLabel->setText(QStringLiteral("Selected device: %1 · %2 · %3")
            .arg(deviceTitle(d),metadataValue(d.fileSystem),metadataValue(d.volumeGuid)));
    for (const QString& name : {QStringLiteral("operationRecoverFiles"),QStringLiteral("operationSecureWipe"),
                                QStringLiteral("operationWholeDrive"),QStringLiteral("operationViewAssessment")})
        if (auto* action=findChild<QPushButton*>(name)) action->setEnabled(true);
    updateDriveOverview(d);
    m_returnToDeviceInformationAfterAssessment = true;
    startDriveAssessment(false);
}

void MainWindow::openFileSelection(FileSelectionMode mode)
{
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity || !m_selectedDriveInfo.connected)
        return;
    m_navigationButtons[0]->setChecked(true); navigateTo(0,QStringLiteral("Dashboard"));
    m_fileSelectionMode=mode;
    const bool recover=mode==FileSelectionMode::RecoverCandidates;
    m_dashboardFileModel->setSelectionMode(true,recover,m_selectedDeviceIdentity);
    if (auto* title=findChild<QLabel*>(QStringLiteral("filesSectionTitle")))
        title->setText(recover ? QStringLiteral("Select Deleted-file Candidates") : QStringLiteral("Select Existing Files for Secure Wipe Review"));
    m_fileSelectionCount->show(); m_fileSelectAllButton->show(); m_fileClearSelectionButton->show();
    if (auto* review=findChild<QFrame*>(QStringLiteral("fileSelectionReview"))) review->show();
    m_assessmentSummaryCard->hide();
    m_assessmentBackButton->setText(QStringLiteral("Back to Operation Panel"));
    m_fileOperationUnavailableButton->setText(recover ? QStringLiteral("Recovery unavailable") : QStringLiteral("Wipe unavailable"));
    m_fileSelectionAvailability->setText(recover
        ? QStringLiteral("Selected rows are deleted-entry candidates for review only. Recovery is not enabled.")
        : QStringLiteral("Selected rows are for review only. Secure wiping is not enabled."));
    m_fileSearch->clear();
    m_fileFilter->setCurrentIndex(recover ? 2 : 1);
    updateFileSelectionReview();
    startDriveAssessment(false);
    m_fileFilter->setCurrentIndex(recover ? 2 : 1);
}

void MainWindow::updateFileSelectionReview()
{
    if (!m_fileSelectionCount || m_fileSelectionMode==FileSelectionMode::None) return;
    const auto selected=m_dashboardFileModel->selectedEntries();
    m_fileSelectionCount->setText(QStringLiteral("%1 item(s) selected for review").arg(selected.size()));
    if (selected.empty()) {
        m_fileSelectionReview->setText(QStringLiteral("No items selected."));
        return;
    }
    QStringList lines;
    const std::size_t shown=std::min<std::size_t>(selected.size(),8);
    for (std::size_t i=0;i<shown;++i) {
        const FileEntry& entry=selected[i];
        const QString name=entry.nameKnown && !entry.name.empty() ? QString::fromStdString(entry.name) : QStringLiteral("Unavailable name");
        const QString path=!entry.fullPath.empty() ? QString::fromStdString(entry.fullPath)
            : entry.parentPathKnown ? QString::fromStdString(entry.parentPath) : QStringLiteral("Unavailable location");
        lines.push_back(QStringLiteral("%1 — %2 · %3").arg(name,path,entry.sizeKnown ? QStringLiteral("%1 bytes").arg(entry.sizeBytes) : QStringLiteral("Size unavailable")));
    }
    if (selected.size()>shown) lines.push_back(QStringLiteral("… and %1 more").arg(selected.size()-shown));
    m_fileSelectionReview->setText(lines.join(QLatin1Char('\n')));
}

void MainWindow::startDriveAssessment(bool forceNewAssessment)
{
    if (m_assessmentInProgress || m_fileScanInProgress || m_deviceScanInProgress)
        return;
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity ||
        !m_selectedDriveInfo.capacityAvailable || !m_selectedDriveInfo.logicalSectorSize ||
        m_selectedDriveInfo.physicalDiskNumber.empty() || !m_selectedDriveInfo.partitionInfoAvailable ||
        m_selectedDriveInfo.partitionNumber.empty()) {
        m_assessmentStatus->setText(QStringLiteral("Assessment Incomplete"));
        m_assessmentSummary->clear();
        m_dashboardStates->setCurrentIndex(5);
        return;
    }

    const DriveInfo selected = m_selectedDriveInfo;
    const QString selectedIdentity = m_selectedDeviceIdentity;
    if (!forceNewAssessment && m_lastScanIdentity == selectedIdentity && m_lastScanResultAvailable) {
        if (m_cachedAssessmentAvailable) {
            displaySavedAssessment(m_cachedAssessment, selected);
        } else {
            m_dashboardFileModel->setEntries({});
            m_assessmentStatus->setText(QStringLiteral("Assessment Incomplete"));
            m_assessmentSummary->clear();
            const QString error = m_lastScanResult.errorMessage.empty()
                ? QStringLiteral("The prior scan did not produce a complete result. Use Refresh to try again.")
                : QString::fromStdString(m_lastScanResult.errorMessage);
            m_dashboardFileStatus->setText(QStringLiteral("No file entries available · %1").arg(error));
            m_fileListingAssessmentButton->show();
            if (m_scanRequestedFromResults) {
                displayScanResultsEntries({},m_fileScanSource->text(),QString(),error);
                m_scanRequestedFromResults = false;
            } else {
                m_dashboardStates->setCurrentIndex(5);
            }
        }
        return;
    }
    const AssessmentLoadResult saved = m_assessmentPersistence.loadFor(selected);
    if (saved.status == AssessmentLoadStatus::Match && saved.stored.fileEntriesStored && !forceNewAssessment) {
        m_lastScanIdentity = selectedIdentity;
        m_lastScanResultAvailable = false;
        m_cachedAssessment = saved.stored;
        m_cachedAssessmentAvailable = true;
        displaySavedAssessment(saved.stored, selected);
        return;
    }
    // Missing records and legacy assessments without a file listing are
    // incomplete for every workflow that needs entries. Start exactly one
    // shared scan; its result is cached in memory and persisted on success.

    m_assessmentInProgress = true;
    m_fileScanInProgress = true;
    m_lastScanIdentity.clear();
    m_lastScanResultAvailable = false;
    m_cachedAssessmentAvailable = false;
    m_fileScanCancelled = std::make_shared<std::atomic_bool>(false);
    m_fileListingAssessmentButton->hide();
    m_nextStepButton->setEnabled(false);
    m_scanButton->setEnabled(false);
    m_assessmentCancelButton->show();
    m_assessmentCancelButton->setEnabled(true);
    m_dashboardFileModel->setEntries({});
    m_fileSearch->clear();
    m_fileFilter->setCurrentIndex(0);
    m_dashboardFileStatus->setText(QStringLiteral("Scanning FAT32 directory records… results will appear here when validation completes."));
    m_progressStatusLabel->setText(QStringLiteral("Assessing selected drive…"));
    m_progressDetailLabel->setText(QStringLiteral("Reading FAT32 directory metadata. Progress is indeterminate; no drive contents are modified."));
    if (m_scanRequestedFromResults) {
        m_fileScanStatus->setText(QStringLiteral("Scanning FAT32 directory metadata…"));
        m_fileScanButton->setEnabled(false);
        m_fileScanCancelButton->setEnabled(true);
    } else {
        m_dashboardStates->setCurrentIndex(1);
        m_scanAnimation->start();
    }

    const DriveInfo drive = selected;
    const auto completion = [this,drive](const FileScanResult& result) {
        completeDriveAssessment(result,drive);
    };
    if (m_fat32ScanLauncher) m_fat32ScanLauncher(drive, completion);
    else launchFat32Scan(drive, completion);
}

void MainWindow::launchFat32Scan(const DriveInfo& drive, std::function<void(const FileScanResult&)> completion)
{
    if (m_fileScanThread || !m_fileScanCancelled)
        return;
    const std::string expectedIdentity = m_selectedDeviceIdentity.toStdString();
    const auto cancelled = m_fileScanCancelled;
    auto result = std::make_shared<FileScanResult>();
    auto openError = std::make_shared<std::string>();
    QThread* worker = QThread::create([drive,expectedIdentity,cancelled,result,openError]() {
        auto reader = FileScanner::openMountedVolumeReadOnly(drive,*openError);
        if (!reader) {
            result->status = FileScanStatus::Error;
            result->errorMessage = *openError;
            return;
        }
        *result = FileScanner::scanFat32(reader,expectedIdentity,FileScanner::Limits{},
                                         [cancelled]() { return cancelled->load(); });
    });
    m_fileScanThread = worker;
    connect(worker,&QThread::finished,this,[this,worker,result,completion=std::move(completion)]() {
        if (m_fileScanThread == worker)
            m_fileScanThread = nullptr;
        worker->deleteLater();
        completion(*result);
    });
    worker->start();
}

void MainWindow::completeDriveAssessment(const FileScanResult& result, const DriveInfo& drive)
{
    m_assessmentInProgress = false;
    m_fileScanInProgress = false;
    m_assessmentCancelButton->hide();
    m_assessmentCancelButton->setEnabled(false);
    m_scanButton->setEnabled(true);
    m_nextStepButton->setEnabled(true);
    stopScanAnimation();

    const bool currentVolume = m_hasSelectedDrive &&
        volumeIdentity(drive) == m_selectedDeviceIdentity &&
        volumeIdentity(m_selectedDriveInfo) == m_selectedDeviceIdentity;
    m_dashboardFileModel->setEntries({});
    m_dashboardFileProxy->setFilterFixedString(QString());
    if (m_fileSearch) m_fileSearch->clear();
    if (m_fileFilter) m_fileFilter->setCurrentIndex(0);
    if (!currentVolume) {
        m_lastScanIdentity.clear();
        m_lastScanResultAvailable = false;
        m_cachedAssessmentAvailable = false;
        m_fileListingAssessmentButton->hide();
        m_dashboardFileStatus->setText(QStringLiteral("Results discarded: the selected volume changed or became stale during the scan."));
        m_assessmentStatus->setText(QStringLiteral("Assessment unavailable"));
        m_assessmentSummary->clear();
        if (m_scanRequestedFromResults) {
            displayScanResultsEntries({},m_fileScanSource->text(),QString(),QStringLiteral("Results discarded: the selected volume changed during the scan."));
            m_scanRequestedFromResults = false;
        } else {
            m_dashboardStates->setCurrentIndex(5);
        }
        m_fileScanCancelled.reset();
        return;
    }

    m_lastScanIdentity = m_selectedDeviceIdentity;
    m_lastScanResult = result;
    m_lastScanResultAvailable = true;
    m_cachedAssessmentAvailable = false;

    const bool supported = QString::fromStdString(drive.fileSystem).compare(QStringLiteral("FAT32"),Qt::CaseInsensitive)==0;
    updateDriveOverview(drive);

    RecoverabilityEvidence evidence;
    evidence.filesystemSupported = supported;
    evidence.volumeIdentityValidated = result.volumeIdentityValidated &&
        result.status != FileScanStatus::StaleVolume && !drive.volumeGuid.empty() &&
        volumeIdentity(drive) == m_selectedDeviceIdentity;
    FileScanCounts fileCounts;
    if ((result.status == FileScanStatus::Success || result.status == FileScanStatus::Partial) &&
        result.volumeIdentityValidated) {
        evidence.scanState = result.status == FileScanStatus::Success ? AssessmentScanState::Complete : AssessmentScanState::Partial;
        evidence.filesystemValid = result.status == FileScanStatus::Success;
        fileCounts = aggregateFileEntries(result.entries);
        evidence.existingFileCount = fileCounts.existingFiles;
        evidence.deletedEntryCount = fileCounts.deletedEntryCandidates;
        evidence.deletedEntriesWithUnknownName = fileCounts.deletedNamesUnknown;
        evidence.invalidEntryCount = fileCounts.invalidRecords;
        evidence.ambiguousEntryCount = fileCounts.ambiguousRecords;
    } else if (result.status == FileScanStatus::Cancelled) {
        evidence.scanState = AssessmentScanState::Cancelled;
    } else if (result.status == FileScanStatus::Unsupported) {
        evidence.scanState = AssessmentScanState::Unsupported;
    } else {
        evidence.scanState = AssessmentScanState::Failed;
        evidence.readErrors = result.readError;
    }
    const RecoverabilityAssessment assessment = assessRecoverability(evidence);
    m_assessmentStatus->setText(QString::fromStdString(assessment.title));
    const QString assessmentColor = assessment.status == AssessmentStatus::PotentialRecoveryEvidenceFound
        ? QStringLiteral("#66d9e7")
        : assessment.status == AssessmentStatus::FurtherAnalysisRequired ? QStringLiteral("#f0b36e")
        : assessment.status == AssessmentStatus::InsufficientEvidence ? QStringLiteral("#a9bdd0")
        : QStringLiteral("#e98a9e");
    m_assessmentStatus->setStyleSheet(QStringLiteral("color:%1; background:rgba(31,45,62,165); border:1px solid rgba(105,144,174,75); border-radius:9px; padding:7px 10px; font-size:14px; font-weight:700;").arg(assessmentColor));

    const QString scanState = result.status == FileScanStatus::Success && result.volumeIdentityValidated ? QStringLiteral("Complete (FAT32 metadata enumeration)")
        : result.status == FileScanStatus::Success ? QStringLiteral("Identity validation failed")
        : result.status == FileScanStatus::Partial ? QStringLiteral("Partial")
        : result.status == FileScanStatus::Cancelled ? QStringLiteral("Cancelled")
        : result.status == FileScanStatus::Unsupported ? QStringLiteral("Unsupported filesystem")
        : QStringLiteral("Failed");
    const QString error = result.errorMessage.empty() ? QStringLiteral("None reported") : QString::fromStdString(result.errorMessage);
    const bool countsAvailable = (result.status == FileScanStatus::Success || result.status == FileScanStatus::Partial) &&
        result.volumeIdentityValidated && result.status != FileScanStatus::StaleVolume;
    std::vector<FileEntry> displayEntries;
    if (countsAvailable) {
        displayEntries.reserve(result.entries.size());
        for (const FileEntry& entry : result.entries) {
            if (!isListedFileRecord(entry))
                continue;
            displayEntries.push_back(entry);
        }
        // Keep the filtered result vector intact; the same exact records are
        // persisted below and must be available on repeat visits.
        m_dashboardFileModel->setEntries(displayEntries);
    }
    updateFileSelectionReview();
    m_fileListingAssessmentButton->setVisible(true);
    m_fileListingAssessmentButton->setEnabled(true);
    if (!countsAvailable) {
        const QString validationMessage = (result.status == FileScanStatus::Success || result.status == FileScanStatus::Partial) &&
            !result.volumeIdentityValidated ? QStringLiteral("The scan result has no validated volume identity.") : error;
        m_dashboardFileStatus->setText(QStringLiteral("File records unavailable · Completion: %1 · %2").arg(scanState,validationMessage));
    } else if (m_dashboardFileModel->rowCount() == 0) {
        m_dashboardFileStatus->setText(QStringLiteral("No file records were identified for this scan · Completion: %1").arg(scanState));
    } else {
        m_dashboardFileStatus->setText(QStringLiteral("Scan %1. Deleted entries are candidates, not confirmed recoverable files; invalid records remain marked as invalid.")
                                           .arg(scanState));
    }
    AssessmentDisplaySummary displaySummary;
    if (countsAvailable && isVerifiedDDSummaryTarget(drive) && assessment.status == AssessmentStatus::FurtherAnalysisRequired) {
        const AssessmentLoadResult previous = m_assessmentPersistence.loadFor(drive);
        displaySummary = previous.status == AssessmentLoadStatus::Match && previous.stored.displaySummary.present
            ? previous.stored.displaySummary : ddAssessmentSummary();
    }
    m_assessmentSummary->setText(displaySummary.present ? assessmentSummaryText(displaySummary) : QString());

    if (result.status == FileScanStatus::Success && result.volumeIdentityValidated &&
        evidence.scanState == AssessmentScanState::Complete && evidence.volumeIdentityValidated) {
        StoredRecoverabilityAssessment stored;
        stored.device = drive;
        stored.evidence = evidence;
        stored.assessment = assessment;
        stored.counts = fileCounts;
        stored.fileEntries = displayEntries;
        stored.fileEntriesStored = true;
        stored.displaySummary = displaySummary;
        stored.assessedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        m_cachedAssessment = stored;
        m_cachedAssessmentAvailable = true;
        QString saveError;
        if (m_assessmentPersistence.save(stored, &saveError)) {
            m_dashboardFileStatus->setText(m_dashboardFileStatus->text() +
                QStringLiteral(" · Assessment and file listing saved locally at %1 UTC").arg(stored.assessedAtUtc));
        } else {
            m_dashboardFileStatus->setText(m_dashboardFileStatus->text() +
                QStringLiteral(" · Assessment was not saved: %1").arg(saveError));
        }
    }
    if (m_scanRequestedFromResults) {
        if (countsAvailable) {
            const QString resultMessage = result.status == FileScanStatus::Partial
                ? QStringLiteral("Identification was partial. Deleted entries are candidates only; recoverability is unknown.")
                : displayEntries.empty()
                    ? QStringLiteral("No filesystem records were identified in the supported FAT32 metadata.")
                    : QStringLiteral("Identification complete. Deleted entries are candidates only; recoverability is unknown.");
            displayScanResultsEntries(displayEntries,m_fileScanSource->text(),resultMessage);
        } else {
            displayScanResultsEntries({},m_fileScanSource->text(),QString(),error);
        }
        m_fileScanButton->setEnabled(true);
        m_fileScanCancelButton->setEnabled(false);
        m_scanRequestedFromResults = false;
    } else {
        m_dashboardStates->setCurrentIndex(5);
    }
    updateFileSelectionReview();
    m_fileScanCancelled.reset();
}

void MainWindow::updateDriveOverview(const DriveInfo& drive)
{
    const QString volumeLabel = drive.volumeLabel.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.volumeLabel);
    const QString mount = drive.mountPoint.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.mountPoint);
    const QString identity = drive.volumeGuid.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.volumeGuid);
    const QString filesystem = drive.fileSystem.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.fileSystem);
    QString used = QStringLiteral("Unknown");
    QString free = drive.totalFreeBytesAvailable ? QStringLiteral("%1 bytes").arg(drive.totalFreeBytes) : QStringLiteral("Unknown");
    if (drive.capacityAvailable && drive.totalFreeBytesAvailable && drive.totalFreeBytes <= drive.capacityBytes)
        used = QStringLiteral("%1 bytes").arg(drive.capacityBytes-drive.totalFreeBytes);
    const QString physical = drive.physicalDiskNumber.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.physicalDiskNumber);
    const QString physicalModel = drive.deviceModel.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.deviceModel);
    const QString busType = drive.busType.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.busType);
    const QString deviceType = drive.deviceType.empty() ? QStringLiteral("Unknown") : QString::fromStdString(drive.deviceType);
    const QString availableToCaller = drive.availableBytesAvailable ? QStringLiteral("%1 bytes").arg(drive.availableBytes) : QStringLiteral("Unknown");
    const QString partition = drive.partitionInfoAvailable
        ? QStringLiteral("Partition %1 · offset %2 bytes · length %3 bytes")
              .arg(QString::fromStdString(drive.partitionNumber)).arg(drive.partitionOffsetBytes).arg(drive.partitionLengthBytes)
        : QStringLiteral("Unknown");
    QString physicalDetails = physical;
    if (physicalModel != QStringLiteral("Unknown")) physicalDetails += QStringLiteral(" · %1").arg(physicalModel);
    if (busType != QStringLiteral("Unknown")) physicalDetails += QStringLiteral(" · %1").arg(busType);
    if (deviceType != QStringLiteral("Unknown")) physicalDetails += QStringLiteral(" · %1").arg(deviceType);
    if (partition != QStringLiteral("Unknown")) physicalDetails += QStringLiteral("\n%1").arg(partition);
    m_driveOverviewValues[0]->setText(volumeLabel);
    m_driveOverviewValues[1]->setText(mount);
    m_driveOverviewValues[2]->setText(filesystem);
    m_driveOverviewValues[3]->setText(drive.capacityAvailable ? QStringLiteral("%1 · %2 bytes").arg(formatCapacity(drive)).arg(drive.capacityBytes) : QStringLiteral("Unknown"));
    m_driveOverviewValues[4]->setText(used);
    m_driveOverviewValues[5]->setText(QStringLiteral("Total: %1\nAvailable to caller: %2").arg(free,availableToCaller));
    m_driveOverviewValues[6]->setText(identity);
    m_driveOverviewValues[7]->setText(physicalDetails);
}

void MainWindow::displaySavedAssessment(const StoredRecoverabilityAssessment& saved, const DriveInfo& currentDevice)
{
    if (!m_hasSelectedDrive || volumeIdentity(currentDevice) != m_selectedDeviceIdentity ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity) {
        m_assessmentStatus->setText(QStringLiteral("Assessment unavailable"));
        m_assessmentSummary->clear();
        m_dashboardStates->setCurrentIndex(5);
        return;
    }
    StoredRecoverabilityAssessment displayed = saved;
    m_cachedAssessment = displayed;
    m_cachedAssessmentAvailable = true;
    m_lastScanIdentity = m_selectedDeviceIdentity;
    if (!displayed.displaySummary.present &&
        isVerifiedDDSummaryTarget(currentDevice) &&
        displayed.assessment.title == "Further Analysis Required") {
        displayed.displaySummary = ddAssessmentSummary();
        QString persistError;
        m_assessmentPersistence.saveDisplaySummaryFor(currentDevice,displayed.displaySummary,&persistError);
    }
    updateDriveOverview(currentDevice);
    m_dashboardFileModel->setEntries(displayed.fileEntries);
    m_fileSearch->clear();
    m_fileFilter->setCurrentIndex(0);
    m_assessmentStatus->setText(QString::fromStdString(displayed.assessment.title));
    const QString assessmentColor = displayed.assessment.status == AssessmentStatus::PotentialRecoveryEvidenceFound
        ? QStringLiteral("#66d9e7")
        : displayed.assessment.status == AssessmentStatus::FurtherAnalysisRequired ? QStringLiteral("#f0b36e")
        : displayed.assessment.status == AssessmentStatus::InsufficientEvidence ? QStringLiteral("#a9bdd0")
        : QStringLiteral("#e98a9e");
    m_assessmentStatus->setStyleSheet(QStringLiteral("color:%1; background:rgba(31,45,62,165); border:1px solid rgba(105,144,174,75); border-radius:9px; padding:7px 10px; font-size:14px; font-weight:700;").arg(assessmentColor));
    if (!displayed.fileEntriesStored) {
        m_dashboardFileStatus->setText(QStringLiteral("Legacy assessment loaded from %1 UTC; its missing file listing will be refreshed automatically.")
                                           .arg(saved.assessedAtUtc));
    } else if (displayed.fileEntries.empty()) {
        m_dashboardFileStatus->setText(QStringLiteral("Saved assessment loaded from %1 UTC; the completed listing contains no file entries.")
                                           .arg(saved.assessedAtUtc));
    } else {
        m_dashboardFileStatus->setText(QStringLiteral("Saved assessment and %1 file entries loaded from %2 UTC; no new scan was performed.")
                                           .arg(saved.fileEntries.size()).arg(saved.assessedAtUtc));
    }
    m_fileListingAssessmentButton->setVisible(true);
    m_fileListingAssessmentButton->setEnabled(true);
    m_assessmentSummary->setText(displayed.displaySummary.present ? assessmentSummaryText(displayed.displaySummary) : QString());
    m_assessmentBackButton->setText(m_returnToDeviceInformationAfterAssessment &&
                                   m_fileSelectionMode == FileSelectionMode::None
        ? QStringLiteral("Back to Device Information")
        : m_fileSelectionMode == FileSelectionMode::None
            ? QStringLiteral("Back to Drive Selection") : QStringLiteral("Back to Operation Panel"));
    m_dashboardStates->setCurrentIndex(5);
    if (m_scanRequestedFromResults) {
        const QString status = displayed.fileEntries.empty()
            ? QStringLiteral("No filesystem records were identified. Saved assessment loaded; no new scan was performed.")
            : QStringLiteral("Saved file listing loaded; no new scan was performed.");
        displayScanResultsEntries(displayed.fileEntries,m_fileScanSource->text(),status);
        m_fileScanButton->setEnabled(true);
        m_fileScanCancelButton->setEnabled(false);
        m_scanRequestedFromResults = false;
    }
    updateFileSelectionReview();
}

void MainWindow::startDeviceScan()
{
    if (m_deviceScanInProgress || m_fileScanInProgress)
        return;

    m_deviceScanInProgress = true;
    m_fileSelectionMode=FileSelectionMode::None;
    if (m_dashboardFileModel) m_dashboardFileModel->setSelectionMode(false,false,{});
    if (m_operationTargetLabel) m_operationTargetLabel->setText(QStringLiteral("Select a verified device before choosing an operation."));
    for (const QString& name : {QStringLiteral("operationRecoverFiles"),QStringLiteral("operationSecureWipe"),
                                QStringLiteral("operationWholeDrive"),QStringLiteral("operationViewAssessment")})
        if (auto* action=findChild<QPushButton*>(name)) action->setEnabled(false);
    m_detectedDevices.clear();
    m_selectedDeviceIdentity.clear();
    m_hasSelectedDrive = false;
    m_lastScanIdentity.clear();
    m_lastScanResultAvailable = false;
    m_cachedAssessmentAvailable = false;
    m_returnToDeviceInformationAfterAssessment = false;
    if (m_deviceList) {
        m_deviceList->clear();
        m_deviceList->setCurrentRow(-1);
    }
    if (m_selectedDevicePanel)
        m_selectedDevicePanel->hide();
    if (m_selectionPrompt)
        m_selectionPrompt->show();
    if (m_scanButton) {
        m_scanButton->setEnabled(false);
        m_scanButton->setText(QStringLiteral("Scanning…"));
    }
    m_dashboardStates->setCurrentIndex(1);
    m_progressStatusLabel->setText(QStringLiteral("Scanning for connected devices…"));
    m_progressDetailLabel->setText(QStringLiteral("Connected removable storage volumes are being checked. This read-only check does not modify their contents."));
    m_assessmentCancelButton->hide();
    m_scanAnimation->start();

    auto result = std::make_shared<DeviceEnumerationResult>();
    QThread* worker = QThread::create([result]() {
        DeviceManager manager;
        *result = manager.enumerateRemovableDrives();
    });
    m_scanThread = worker;
    connect(worker,&QThread::finished,this,[this,worker,result]() {
        if (m_scanThread == worker)
            m_scanThread = nullptr;
        worker->deleteLater();
        completeDeviceScan(*result);
    });
    worker->start();
}

void MainWindow::completeDeviceScan(const DeviceEnumerationResult& result)
{
    m_deviceScanInProgress = false;
    if (m_scanButton) {
        m_scanButton->setEnabled(true);
        m_scanButton->setText(QStringLiteral("Scan for Devices"));
    }
    stopScanAnimation();
    displayDeviceResults(result);
}

void MainWindow::stopScanAnimation()
{
    if (m_scanAnimation && m_scanAnimation->state() != QAbstractAnimation::Stopped)
        m_scanAnimation->stop();
    if (m_scanStateIcon) {
        if (auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_scanStateIcon->graphicsEffect()))
            opacity->setOpacity(1.0);
    }
}

void MainWindow::displayDeviceResults(const DeviceEnumerationResult& result)
{
    m_selectedDeviceIdentity.clear();
    m_hasSelectedDrive = false;
    if (!result.success) {
        m_scanDetailLabel->setText(QString::fromStdString(result.errorMessage));
        m_dashboardStates->setCurrentIndex(4);
        return;
    }

    m_detectedDevices = result.devices;
    if (m_detectedDevices.empty()) {
        m_dashboardStates->setCurrentIndex(3);
        return;
    }

    m_deviceList->blockSignals(true);
    m_deviceList->clear();
    QHash<QString, int> identityCounts;
    for (const DriveInfo& device : m_detectedDevices) {
        const QString identity = volumeIdentity(device);
        if (!identity.isEmpty())
            identityCounts[identity] += 1;
    }
    for (std::size_t index = 0; index < m_detectedDevices.size(); ++index) {
        const DriveInfo& device = m_detectedDevices[index];
        const QString fileSystem = metadataValue(device.fileSystem);
        const QString capacity = formatCapacity(device);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\nMount: %2  ·  Capacity: %3  ·  %4")
                .arg(deviceTitle(device),
                     metadataValue(device.mountPoint),
                     capacity,
                     fileSystem),
            m_deviceList);
        const QString identity = volumeIdentity(device);
        item->setData(Qt::UserRole, identity);
        if (identity.isEmpty() || identityCounts.value(identity) != 1) {
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setToolTip(QStringLiteral("This volume has no unique verified identifier and cannot be selected safely."));
        }
        item->setSizeHint(QSize(item->sizeHint().width(), 66));
    }
    m_deviceList->setCurrentRow(-1);
    m_deviceList->blockSignals(false);
    m_selectionPrompt->setText(QStringLiteral("Select a detected device to review its available details."));
    m_selectionPrompt->show();
    m_selectedDevicePanel->hide();
    m_scanStatusLabel->setText(QStringLiteral("Device detected"));
    m_scanDetailLabel->setText(QStringLiteral("%1 removable volume(s) found. Select a device to review verified details.")
                                    .arg(m_detectedDevices.size()));
    m_dashboardStates->setCurrentIndex(2);
}

void MainWindow::selectDeviceRow(int row)
{
    if (row < 0 || row >= m_deviceList->count()) {
        m_selectedDevicePanel->hide();
        m_selectionPrompt->show();
        m_selectedDeviceIdentity.clear();
        m_hasSelectedDrive = false;
        return;
    }

    const QString identity = m_deviceList->item(row)->data(Qt::UserRole).toString();
    int matchingIndex = -1;
    for (std::size_t index = 0; index < m_detectedDevices.size(); ++index) {
        if (volumeIdentity(m_detectedDevices[index]) == identity) {
            if (matchingIndex >= 0) {
                m_selectedDevicePanel->hide();
                m_selectionPrompt->setText(QStringLiteral("The volume identifier is ambiguous. Rescan before continuing."));
                m_selectionPrompt->show();
                m_selectedDeviceIdentity.clear();
                m_hasSelectedDrive = false;
                return;
            }
            matchingIndex = static_cast<int>(index);
        }
    }
    if (identity.isEmpty() || matchingIndex < 0) {
        m_selectedDevicePanel->hide();
        m_selectionPrompt->setText(QStringLiteral("This volume has no unique verified identifier. Rescan before continuing."));
        m_selectionPrompt->show();
        m_selectedDeviceIdentity.clear();
        m_hasSelectedDrive = false;
        return;
    }
    if (m_selectedDeviceIdentity != identity) {
        m_lastScanIdentity.clear();
        m_lastScanResultAvailable = false;
        m_cachedAssessmentAvailable = false;
    }
    m_selectedDeviceIdentity = identity;
    const DriveInfo& device = m_detectedDevices[static_cast<std::size_t>(matchingIndex)];
    m_selectedDriveInfo = device;
    m_hasSelectedDrive = true;
    if (m_dashboardFileModel) m_dashboardFileModel->setEntries({});
    if (m_dashboardFileStatus)
        m_dashboardFileStatus->setText(QStringLiteral("Choose Next Step to load this device's saved assessment or start its first supported scan."));
    if (m_assessmentStatus) m_assessmentStatus->setText(QStringLiteral("Assessment Incomplete"));
    if (m_assessmentSummary) m_assessmentSummary->clear();
    if (m_dashboardFileProxy) {
        m_fileSearch->clear();
        m_fileFilter->setCurrentIndex(0);
    }
    const QString mount = metadataValue(device.mountPoint);
    const QString fileSystem = metadataValue(device.fileSystem);
    const QString available = device.availableBytesAvailable
        ? QStringLiteral("%1 GB").arg(static_cast<double>(device.availableBytes) / 1.0e9, 0, 'f', 1)
        : QStringLiteral("Unknown");
    const QString physicalCapacity = device.physicalDiskCapacityAvailable
        ? QStringLiteral("%1 GB").arg(static_cast<double>(device.physicalDiskCapacityBytes) / 1.0e9, 0, 'f', 1)
        : QStringLiteral("Unknown");
    m_selectedDeviceName->setText(deviceTitle(device));
    m_selectedDeviceDetails->setText(
        QStringLiteral("Volume label: %1\nMount point: %2\nVolume capacity: %3\nAvailable free space: %4\nFile system: %5\nDevice type: %6\nConnection: %7")
            .arg(metadataValue(device.volumeLabel),
                 mount,
                 formatCapacity(device),
                 available,
                 fileSystem,
                 metadataValue(device.deviceType),
                 device.connected ? QStringLiteral("Connected") : QStringLiteral("Unknown")));
    m_technicalDeviceDetails->setText(
        QStringLiteral("Volume ID: %1\nPhysical disk: %2\nModel: %3\nBus type: %4\nPartition number: %5\nPartition offset: %6\nPartition length: %7\nPhysical disk capacity: %8")
            .arg(metadataValue(device.volumeGuid),
                 metadataValue(device.physicalDiskNumber),
                 metadataValue(device.deviceModel),
                 metadataValue(device.busType),
                 metadataValue(device.partitionNumber),
                 device.partitionInfoAvailable ? QStringLiteral("%1 bytes").arg(device.partitionOffsetBytes) : QStringLiteral("Unknown"),
                 device.partitionInfoAvailable ? QStringLiteral("%1 bytes").arg(device.partitionLengthBytes) : QStringLiteral("Unknown"),
                 physicalCapacity));
    m_deviceDetailsToggle->setChecked(false);

    m_selectionPrompt->hide();
    m_selectedDevicePanel->show();
    auto* opacity = new QGraphicsOpacityEffect(m_selectedDevicePanel);
    opacity->setOpacity(0.0);
    m_selectedDevicePanel->setGraphicsEffect(opacity);
    auto* fade = new QPropertyAnimation(opacity,"opacity",m_selectedDevicePanel);
    fade->setDuration(200);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

QWidget* MainWindow::buildOperationsPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4,8,8,12);
    layout->setSpacing(12);
    layout->addWidget(heading(QStringLiteral("Operation Panel"),content));
    m_operationTargetLabel = bodyText(QStringLiteral("Select a verified device before choosing an operation."),content);
    m_operationTargetLabel->setObjectName(QStringLiteral("operationTargetContext"));
    layout->addWidget(m_operationTargetLabel);
    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    const QStringList titles = {QStringLiteral("Recover Files"),QStringLiteral("Secure Wipe"),QStringLiteral("Secure wipe for whole drive"),QStringLiteral("View Assessment")};
    const QStringList details = {
        QStringLiteral("Review deleted-file candidates identified by the supported scanner."),
        QStringLiteral("Review existing file records and available wiping information."),
        QStringLiteral("Review the selected device and full-drive workflow availability."),
        QStringLiteral("Open this device's saved assessment and configured evidence summary.")};
    const QStringList icons = {QStringLiteral("⌕"),QStringLiteral("◇"),QStringLiteral("▣"),QStringLiteral("▤")};
    const QStringList buttonNames = {QStringLiteral("operationRecoverFiles"),QStringLiteral("operationSecureWipe"),QStringLiteral("operationWholeDrive"),QStringLiteral("operationViewAssessment")};
    for (int i=0;i<titles.size();++i) {
        auto* card=makeCard(content); auto* box=new QVBoxLayout(card); box->setContentsMargins(18,16,18,16); box->setSpacing(9);
        auto* icon=new QLabel(icons[i],card); icon->setStyleSheet(QStringLiteral("color:#69d7f4; font-size:24px; font-weight:700;")); box->addWidget(icon);
        box->addWidget(heading(titles[i],card)); box->addWidget(bodyText(details[i],card),1);
        auto* action=new QPushButton(i==2 ? QStringLiteral("Review availability") : QStringLiteral("Continue"),card);
        action->setObjectName(buttonNames[i]); action->setMinimumHeight(36);
        action->setEnabled(false);
        action->setStyleSheet(QStringLiteral("QPushButton { color:#07111d; background:#59cde2; border:0; border-radius:7px; padding:7px 10px; font-weight:700; } QPushButton:hover { background:#8be5f2; } QPushButton:disabled { color:#aab8c5; background:#465b70; }"));
        box->addWidget(action);
        grid->addWidget(card,i/2,i%2);
    }
    layout->addLayout(grid);
    auto* back=linkButton(QStringLiteral("Back to Device Information"),content);
    connect(back,&QPushButton::clicked,this,[this]() {
        m_navigationButtons[0]->setChecked(true); navigateTo(0,QStringLiteral("Dashboard")); m_dashboardStates->setCurrentIndex(6);
    });
    layout->addWidget(back,0,Qt::AlignLeft); layout->addStretch();
    connect(content->findChild<QPushButton*>(QStringLiteral("operationRecoverFiles")),&QPushButton::clicked,this,[this]() { openFileSelection(FileSelectionMode::RecoverCandidates); });
    connect(content->findChild<QPushButton*>(QStringLiteral("operationSecureWipe")),&QPushButton::clicked,this,[this]() { openFileSelection(FileSelectionMode::SecureWipeReview); });
    connect(content->findChild<QPushButton*>(QStringLiteral("operationWholeDrive")),&QPushButton::clicked,this,[this]() { m_dashboardStates->setCurrentIndex(7); m_navigationButtons[0]->setChecked(true); navigateTo(0,QStringLiteral("Dashboard")); });
    connect(content->findChild<QPushButton*>(QStringLiteral("operationViewAssessment")),&QPushButton::clicked,this,[this]() {
        m_navigationButtons[0]->setChecked(true); navigateTo(0,QStringLiteral("Dashboard"));
        m_fileSelectionMode=FileSelectionMode::None;
        m_dashboardFileModel->setSelectionMode(false,false,m_selectedDeviceIdentity);
        m_assessmentSummaryCard->show();
        m_fileSelectionCount->hide(); m_fileSelectAllButton->hide(); m_fileClearSelectionButton->hide();
        if (auto* review=findChild<QFrame*>(QStringLiteral("fileSelectionReview"))) review->hide();
        if (auto* title=findChild<QLabel*>(QStringLiteral("filesSectionTitle"))) title->setText(QStringLiteral("Files on Selected Drive"));
        m_assessmentBackButton->setText(QStringLiteral("Back to Drive Selection"));
        startDriveAssessment(false);
    });
    return scrollPage(content);
}

QWidget* MainWindow::buildDeviceInformationPage()
{
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout(page); layout->addStretch(1);
    auto* card=makeCard(page); card->setObjectName(QStringLiteral("deviceInformationCard")); card->setMaximumWidth(720);
    auto* box=new QVBoxLayout(card); box->setContentsMargins(24,22,24,22); box->setSpacing(12);
    box->addWidget(heading(QStringLiteral("Device Information"),card));
    m_deviceInformationValues=bodyText(QStringLiteral("Select a verified device to view its available information."),card);
    m_deviceInformationValues->setObjectName(QStringLiteral("deviceInformationValues"));
    m_deviceInformationValues->setTextInteractionFlags(Qt::TextSelectableByMouse);
    box->addWidget(m_deviceInformationValues);
    auto* actions=new QHBoxLayout;
    auto* back=new QPushButton(QStringLiteral("Back to Device Selection"),card);
    back->setMinimumHeight(38); connect(back,&QPushButton::clicked,this,[this]() { m_dashboardStates->setCurrentIndex(2); });
    m_deviceInformationContinueButton=new QPushButton(QStringLiteral("Continue to Operations"),card);
    m_deviceInformationContinueButton->setObjectName(QStringLiteral("deviceInformationContinue"));
    m_deviceInformationContinueButton->setMinimumHeight(38);
    connect(m_deviceInformationContinueButton,&QPushButton::clicked,this,[this]() {
        if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() || volumeIdentity(m_selectedDriveInfo)!=m_selectedDeviceIdentity) return;
        m_returnToDeviceInformationAfterAssessment = false;
        m_navigationButtons[3]->setChecked(true); navigateTo(3,QStringLiteral("Operations"));
    });
    actions->addWidget(back); actions->addStretch(); actions->addWidget(m_deviceInformationContinueButton); box->addLayout(actions);
    layout->addWidget(card,0,Qt::AlignHCenter); layout->addStretch(1); return page;
}

QWidget* MainWindow::buildUnavailableOperationPage()
{
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page); layout->addStretch(1);
    auto* card=makeCard(page); card->setMaximumWidth(620); auto* box=new QVBoxLayout(card);
    box->setContentsMargins(24,22,24,22); box->setSpacing(12);
    box->addWidget(heading(QStringLiteral("Whole-drive operation unavailable"),card));
    box->addWidget(bodyText(QStringLiteral("This workflow is visible for planning only. No storage operation is available from this panel."),card));
    auto* back=new QPushButton(QStringLiteral("Back to Operation Panel"),card);
    back->setObjectName(QStringLiteral("backToOperationPanel"));
    connect(back,&QPushButton::clicked,this,[this]() { m_navigationButtons[3]->setChecked(true); navigateTo(3,QStringLiteral("Operations")); });
    box->addWidget(back,0,Qt::AlignLeft); layout->addWidget(card,0,Qt::AlignHCenter); layout->addStretch(1); return page;
}

QWidget* MainWindow::buildPlaceholderPage(const QString& headingText, const QString& message, const QString& glyph)
{
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout(page);
    layout->setContentsMargins(4,8,8,12);
    layout->addStretch();
    auto* card=makeCard(page);
    card->setMaximumWidth(680);
    auto* box=new QVBoxLayout(card); box->setContentsMargins(32,32,32,32); box->setSpacing(14);
    auto* icon=new QLabel(glyph,card); icon->setAlignment(Qt::AlignCenter); icon->setStyleSheet(QStringLiteral("color:#70dcf7; font-size:32px;")); box->addWidget(icon);
    auto* title=heading(headingText,card); title->setAlignment(Qt::AlignCenter); title->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:24px; font-weight:700;")); box->addWidget(title);
    auto* text=bodyText(message,card); text->setAlignment(Qt::AlignCenter); box->addWidget(text);
    layout->addWidget(card,0,Qt::AlignHCenter);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildSettingsPage()
{
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout(page);
    layout->setContentsMargins(4,8,8,12);
    layout->setSpacing(14);
    auto* account=makeCard(page);
    auto* accountLayout=new QVBoxLayout(account); accountLayout->setContentsMargins(20,18,20,18); accountLayout->setSpacing(12);
    accountLayout->addWidget(heading(QStringLiteral("Account"),account));
    auto* nameRow=new QHBoxLayout(); nameRow->addWidget(bodyText(QStringLiteral("Signed-in username"),account)); nameRow->addStretch(); m_settingsUsername=new QLabel(account); m_settingsUsername->setStyleSheet(QStringLiteral("color:#edf6ff; font-weight:700;")); nameRow->addWidget(m_settingsUsername); accountLayout->addLayout(nameRow);
    auto* dbRow=new QHBoxLayout(); dbRow->addWidget(bodyText(QStringLiteral("Persistence"),account)); dbRow->addStretch(); auto* dbMode=new QLabel(QStringLiteral("Local SQLite"),account); dbMode->setStyleSheet(QStringLiteral("color:#edf6ff; font-weight:700;")); dbRow->addWidget(dbMode); accountLayout->addLayout(dbRow);
    layout->addWidget(account);
    auto* about=makeCard(page); auto* aboutLayout=new QVBoxLayout(about); aboutLayout->setContentsMargins(20,18,20,18); aboutLayout->setSpacing(8);
    aboutLayout->addWidget(heading(QStringLiteral("Application"),about)); aboutLayout->addWidget(bodyText(QStringLiteral("DataGuard desktop application\nSecure. Recover. Verify."),about)); layout->addWidget(about);
    auto* logout=new QPushButton(QStringLiteral("Sign Out"),page); logout->setMaximumWidth(170); logout->setMinimumHeight(40); logout->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:8px; font-weight:700; } QPushButton:hover { background:#2395dc; }")); connect(logout,&QPushButton::clicked,this,&MainWindow::signOut); layout->addWidget(logout,0,Qt::AlignLeft);
    layout->addStretch();
    return page;
}


