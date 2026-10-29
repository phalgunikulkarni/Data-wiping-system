#include "MainWindow.h"
#include "ImageBackgroundWidget.h"

#include <QFrame>
#include <QHash>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>
#include <atomic>

namespace {
const QString cardStyle = QStringLiteral(
    "QFrame#contentCard { background-color:rgba(5,18,43,226); border:1px solid rgba(122,177,211,75); border-radius:14px; }"
    "QFrame#quickCard { background-color:rgba(5,18,43,226); border:1px solid rgba(122,177,211,75); border-radius:12px; }");

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
}

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
    sidebar->setStyleSheet(QStringLiteral("QFrame#sidebar { background-color:rgba(4,15,38,232); border:1px solid rgba(122,177,211,65); border-radius:16px; }"));
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
    header->setStyleSheet(QStringLiteral("QFrame#header { background-color:rgba(4,15,38,224); border:1px solid rgba(122,177,211,65); border-radius:14px; }"));
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
    selectedLayout->addWidget(bodyText(QStringLiteral("FAT32 deleted-entry identification is read-only. It does not establish recoverability or enable storage operations."),m_selectedDevicePanel));
    auto* identifyEntries = new QPushButton(QStringLiteral("Identify FAT32 Entries"),m_selectedDevicePanel);
    identifyEntries->setMinimumHeight(38);
    identifyEntries->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:8px; padding:8px 12px; font-weight:700; } QPushButton:hover { background:#2395dc; }"));
    selectedLayout->addWidget(identifyEntries);
    connect(identifyEntries,&QPushButton::clicked,this,[this]() {
        m_navigationButtons[2]->setChecked(true);
        navigateTo(2,QStringLiteral("Scan Results"));
        startDeletedEntryScan();
    });
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

    layout->addWidget(m_dashboardStates,1);
    m_dashboardStates->setCurrentIndex(0);
    return page;
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
    if (m_fileScanInProgress)
        return;
    if (!m_hasSelectedDrive || m_selectedDeviceIdentity.isEmpty() ||
        volumeIdentity(m_selectedDriveInfo) != m_selectedDeviceIdentity) {
        m_fileScanStatus->setText(QStringLiteral("No current, uniquely identified volume is selected. Return to Dashboard and rescan devices."));
        return;
    }
    m_fileScanInProgress = true;
    m_fileScanCancelled = std::make_shared<std::atomic_bool>(false);
    m_fileScanButton->setEnabled(false);
    m_fileScanCancelButton->setEnabled(true);
    m_fileResultsList->clear();
    const QString sourceLabel = QStringLiteral("Source volume: %1 · %2 · %3")
        .arg(QString::fromStdString(m_selectedDriveInfo.volumeLabel).trimmed().isEmpty()
                 ? QStringLiteral("Unnamed volume") : QString::fromStdString(m_selectedDriveInfo.volumeLabel),
             QString::fromStdString(m_selectedDriveInfo.mountPoint),
             QString::fromStdString(m_selectedDriveInfo.fileSystem));
    m_fileScanSource->setText(sourceLabel);
    m_fileScanStatus->setText(QStringLiteral("Reading FAT32 directory metadata…"));

    const DriveInfo selected = m_selectedDriveInfo;
    const std::string expected = m_selectedDeviceIdentity.toStdString();
    auto cancelled = m_fileScanCancelled;
    auto result = std::make_shared<FileScanResult>();
    auto openError = std::make_shared<std::string>();
    QThread* worker = QThread::create([selected, expected, cancelled, result, openError]() {
        auto reader = FileScanner::openMountedVolumeReadOnly(selected, *openError);
        if (!reader) {
            result->status = FileScanStatus::Error;
            result->errorMessage = *openError;
            return;
        }
        *result = FileScanner::scanFat32(reader, expected, FileScanner::Limits{},
                                         [cancelled]() { return cancelled->load(); });
    });
    m_fileScanThread = worker;
    connect(worker,&QThread::finished,this,[this,worker,result,sourceLabel]() {
        if (m_fileScanThread == worker)
            m_fileScanThread = nullptr;
        worker->deleteLater();
        completeDeletedEntryScan(*result,sourceLabel);
    });
    worker->start();
}

void MainWindow::completeDeletedEntryScan(const FileScanResult& result, const QString& sourceVolume)
{
    m_fileScanInProgress = false;
    m_fileScanButton->setEnabled(true);
    m_fileScanCancelButton->setEnabled(false);
    m_fileScanSource->setText(sourceVolume);
    m_fileResultsList->clear();
    if (result.status != FileScanStatus::Success) {
        QString message = QString::fromStdString(result.errorMessage);
        if (message.isEmpty()) message = QStringLiteral("FAT32 identification could not be completed.");
        m_fileScanStatus->setText(message);
        m_fileScanCancelled.reset();
        return;
    }
    int deletedCount = 0;
    for (const FileEntry& entry : result.entries) {
        if (entry.status == FileStatus::DELETED) ++deletedCount;
        const QString state = entry.status == FileStatus::DELETED ? QStringLiteral("Deleted entry candidate")
            : entry.status == FileStatus::INVALID ? QStringLiteral("Invalid / ambiguous")
            : QStringLiteral("Existing entry");
        const QString name = entry.nameKnown && !entry.name.empty()
            ? QString::fromUtf8(entry.name.data(),static_cast<qsizetype>(entry.name.size()))
            : QStringLiteral("Unknown name");
        const QString path = entry.fullPath.empty() ? name
            : QString::fromUtf8(entry.fullPath.data(),static_cast<qsizetype>(entry.fullPath.size()));
        const QString location = entry.fullPath.empty()
            ? QStringLiteral("Parent directory: %1").arg(!entry.parentPathKnown
                  ? QStringLiteral("Unknown")
                  : entry.parentPath.empty() ? QStringLiteral("volume root")
                  : QString::fromUtf8(entry.parentPath.data(),static_cast<qsizetype>(entry.parentPath.size())))
            : path;
        const QString size = entry.sizeKnown ? QStringLiteral("%1 bytes").arg(entry.sizeBytes) : QStringLiteral("Unknown size");
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 · %4")
            .arg(state,location,size,QString::fromStdString(entry.reason)),m_fileResultsList);
        item->setToolTip(QString::fromStdString(entry.reason));
        item->setData(Qt::UserRole,QString::fromStdString(entry.identity));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setSizeHint(QSize(100,72));
    }
    m_fileScanStatus->setText(QStringLiteral("Identification complete: %1 deleted-entry candidate(s), %2 total directory record(s). No recoverability claim is made.")
                                   .arg(deletedCount).arg(result.entries.size()));
    if (result.entries.empty())
        m_fileScanStatus->setText(QStringLiteral("No directory entries were identified in the supported FAT32 metadata."));
    m_fileScanCancelled.reset();
}

void MainWindow::startDeviceScan()
{
    if (m_deviceScanInProgress)
        return;

    m_deviceScanInProgress = true;
    m_detectedDevices.clear();
    m_selectedDeviceIdentity.clear();
    m_hasSelectedDrive = false;
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
        return;
    }
    m_selectedDeviceIdentity = identity;
    const DriveInfo& device = m_detectedDevices[static_cast<std::size_t>(matchingIndex)];
    m_selectedDriveInfo = device;
    m_hasSelectedDrive = true;
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
    layout->setSpacing(14);
    layout->addWidget(bodyText(QStringLiteral("Review the intended workflows. Storage operations are not enabled in this dashboard."), content));
    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    const QStringList titles = {QStringLiteral("Full-drive wiping"),QStringLiteral("Single-file wiping"),QStringLiteral("File recovery"),QStringLiteral("Drive recovery")};
    const QStringList details = {
        QStringLiteral("Erase a supported device after target validation and safety review."),
        QStringLiteral("Securely erase an individual file where the storage technology permits."),
        QStringLiteral("Search for supported deleted files and restore to a separate destination."),
        QStringLiteral("Inspect a drive for recovery candidates and choose a separate destination.")};
    for (int i=0;i<titles.size();++i) {
        auto* card=makeCard(content); auto* box=new QVBoxLayout(card); box->setContentsMargins(18,16,18,16); box->setSpacing(9);
        box->addWidget(heading(titles[i],card)); box->addWidget(bodyText(details[i],card),1);
        auto* state=new QLabel(QStringLiteral("NOT AVAILABLE YET"),card); state->setStyleSheet(QStringLiteral("color:#ff9daf; font-size:10px; letter-spacing:1px; font-weight:700;")); box->addWidget(state);
        grid->addWidget(card,i/2,i%2);
    }
    layout->addLayout(grid); layout->addStretch();
    return scrollPage(content);
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


