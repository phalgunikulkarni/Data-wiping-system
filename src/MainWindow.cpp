#include "MainWindow.h"
#include "ImageBackgroundWidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

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

QWidget* quickActionCard(const QString& glyph, const QString& title, const QString& description,
                         const std::function<void()>& onClick, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("quickCard"));
    frame->setStyleSheet(cardStyle);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16,14,16,12);
    layout->setSpacing(8);
    auto* icon = new QLabel(glyph, frame);
    icon->setStyleSheet(QStringLiteral("color:#6bd7f5; font-size:21px; font-weight:700;"));
    auto* titleLabel = heading(title, frame);
    titleLabel->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:15px; font-weight:700;"));
    auto* descriptionLabel = bodyText(description, frame);
    auto* status = new QLabel(QStringLiteral("NOT AVAILABLE YET"), frame);
    status->setStyleSheet(QStringLiteral("color:#ff9daf; font-size:10px; letter-spacing:1px; font-weight:700;"));
    auto* button = linkButton(QStringLiteral("Details  ›"), frame);
    QObject::connect(button, &QPushButton::clicked, frame, [onClick]() { onClick(); });
    layout->addWidget(icon);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel, 1);
    layout->addWidget(status);
    layout->addWidget(button);
    return frame;
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
    auto* sideFooter = new QLabel(QStringLiteral("LOCAL ACCOUNT  •  SQLITE"), sidebar);
    sideFooter->setStyleSheet(QStringLiteral("color:#6886a3; font-size:9px; letter-spacing:1px; padding:7px;"));
    sideLayout->addWidget(sideFooter);
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
    m_pages->addWidget(buildPlaceholderPage(QStringLiteral("Scan Results"), QStringLiteral("No scan results are available yet. Connect a device and perform a supported scan when that feature is enabled."), QStringLiteral("⌕")));
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
    m_welcomeLabel->setText(m_signedInUser.isEmpty() ? QStringLiteral("Welcome back!") : QStringLiteral("Welcome back, %1!").arg(m_signedInUser));
    m_settingsUsername->setText(m_signedInUser.isEmpty() ? QStringLiteral("No active session") : m_signedInUser);
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
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(2,4,8,12);
    layout->setSpacing(14);

    auto* welcomeCard = makeCard(content);
    auto* welcomeLayout = new QVBoxLayout(welcomeCard);
    welcomeLayout->setContentsMargins(22,17,22,17);
    welcomeLayout->setSpacing(6);
    m_welcomeLabel = heading(QStringLiteral("Welcome back!"), welcomeCard);
    m_welcomeLabel->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:23px; font-weight:700;"));
    welcomeLayout->addWidget(m_welcomeLabel);
    welcomeLayout->addWidget(bodyText(QStringLiteral("Manage your connected devices and data securely."), welcomeCard));
    layout->addWidget(welcomeCard);

    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(14);
    auto* deviceCard = makeCard(content);
    auto* deviceLayout = new QVBoxLayout(deviceCard);
    deviceLayout->setContentsMargins(18,16,18,15);
    auto* deviceTitle = heading(QStringLiteral("Device overview"), deviceCard);
    deviceLayout->addWidget(deviceTitle);
    auto* deviceState = bodyText(QStringLiteral("Device detection is not connected yet."), deviceCard);
    deviceState->setStyleSheet(QStringLiteral("color:#e2edf8; font-size:14px; font-weight:600;"));
    deviceLayout->addWidget(deviceState);
    deviceLayout->addWidget(bodyText(QStringLiteral("No device selected  ·  Capacity and filesystem unavailable"), deviceCard));
    deviceLayout->addStretch();
    auto* devicesLink = linkButton(QStringLiteral("Connect Device  ›"), deviceCard);
    connect(devicesLink, &QPushButton::clicked, this, [this]() { m_navigationButtons[1]->click(); });
    deviceLayout->addWidget(devicesLink);
    summaryRow->addWidget(deviceCard, 1);

    auto* scanCard = makeCard(content);
    auto* scanLayout = new QVBoxLayout(scanCard);
    scanLayout->setContentsMargins(18,16,18,15);
    scanLayout->addWidget(heading(QStringLiteral("Scan summary"), scanCard));
    auto* scanEmpty = bodyText(QStringLiteral("No scan results yet."), scanCard);
    scanEmpty->setStyleSheet(QStringLiteral("color:#e2edf8; font-size:14px; font-weight:600;"));
    scanLayout->addWidget(scanEmpty);
    scanLayout->addWidget(bodyText(QStringLiteral("A scan will only appear here after an authorized scan feature is available."), scanCard));
    scanLayout->addStretch();
    auto* scanLink = linkButton(QStringLiteral("View Scan Results  ›"), scanCard);
    connect(scanLink, &QPushButton::clicked, this, [this]() { m_navigationButtons[2]->click(); });
    scanLayout->addWidget(scanLink);
    summaryRow->addWidget(scanCard, 1);
    layout->addLayout(summaryRow);

    auto* quickTitle = heading(QStringLiteral("Quick actions"), content);
    layout->addWidget(quickTitle);
    auto* quickGrid = new QGridLayout;
    quickGrid->setSpacing(12);
    const QString unavailableMessage = QStringLiteral("This operation will be available after the storage engine safety review.");
    const auto showUnavailable = [this,unavailableMessage]() { QMessageBox::information(this, QStringLiteral("Not available yet"), unavailableMessage); };
    quickGrid->addWidget(quickActionCard(QStringLiteral("▣"), QStringLiteral("Wipe Full Drive"), QStringLiteral("Securely erase a supported storage device."), showUnavailable, content),0,0);
    quickGrid->addWidget(quickActionCard(QStringLiteral("⌕"), QStringLiteral("Wipe a File"), QStringLiteral("Erase a selected file where supported."), showUnavailable, content),0,1);
    quickGrid->addWidget(quickActionCard(QStringLiteral("↗"), QStringLiteral("Recover a File"), QStringLiteral("Find and restore supported deleted files."), showUnavailable, content),1,0);
    quickGrid->addWidget(quickActionCard(QStringLiteral("▤"), QStringLiteral("Recover a Drive"), QStringLiteral("Review recovery candidates from a device."), showUnavailable, content),1,1);
    layout->addLayout(quickGrid);

    auto* lowerRow = new QHBoxLayout;
    lowerRow->setSpacing(14);
    auto* activityCard = makeCard(content);
    auto* activityLayout = new QVBoxLayout(activityCard);
    activityLayout->setContentsMargins(18,15,18,14);
    auto* activityHeader = new QHBoxLayout;
    activityHeader->addWidget(heading(QStringLiteral("Recent activity"), activityCard));
    activityHeader->addStretch();
    auto* activityLink = linkButton(QStringLiteral("Activity Log  ›"), activityCard);
    activityHeader->addWidget(activityLink);
    activityLayout->addLayout(activityHeader);
    activityLayout->addWidget(bodyText(QStringLiteral("No activity recorded yet."), activityCard));
    connect(activityLink, &QPushButton::clicked, this, [this]() { m_navigationButtons[4]->click(); });
    lowerRow->addWidget(activityCard, 2);

    auto* certCard = makeCard(content);
    auto* certLayout = new QVBoxLayout(certCard);
    certLayout->setContentsMargins(18,15,18,14);
    auto* certHeader = new QHBoxLayout;
    certHeader->addWidget(heading(QStringLiteral("Certificates"), certCard));
    certHeader->addStretch();
    auto* certLink = linkButton(QStringLiteral("View  ›"), certCard);
    certHeader->addWidget(certLink);
    certLayout->addLayout(certHeader);
    certLayout->addWidget(bodyText(QStringLiteral("No certificates generated yet."), certCard));
    connect(certLink, &QPushButton::clicked, this, [this]() { m_navigationButtons[5]->click(); });
    lowerRow->addWidget(certCard, 1);
    layout->addLayout(lowerRow);

    auto* assistantCard = makeCard(content);
    auto* assistantLayout = new QHBoxLayout(assistantCard);
    assistantLayout->setContentsMargins(18,12,18,12);
    assistantLayout->addWidget(heading(QStringLiteral("✦  DataGuard Assistant"), assistantCard));
    assistantLayout->addWidget(bodyText(QStringLiteral("Guidance and explanations will be available in a future update."), assistantCard),1);
    auto* assistantLink = linkButton(QStringLiteral("Open Assistant  ›"), assistantCard);
    assistantLayout->addWidget(assistantLink);
    connect(assistantLink, &QPushButton::clicked, this, [this]() { m_navigationButtons[6]->click(); });
    layout->addWidget(assistantCard);
    layout->addStretch();
    return scrollPage(content);
}

QWidget* MainWindow::buildDevicesPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4,8,8,12);
    layout->setSpacing(14);

    auto* intro = makeCard(page);
    auto* introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(26,22,26,22);
    introLayout->setSpacing(11);
    auto* icon = new QLabel(QStringLiteral("▣"), intro);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(66,66);
    icon->setStyleSheet(QStringLiteral("color:#70dcf7; background:rgba(27,94,135,100); border:1px solid rgba(112,220,247,95); border-radius:16px; font-size:34px;"));
    introLayout->addWidget(icon,0,Qt::AlignHCenter);
    auto* title = heading(QStringLiteral("Connect Your Device"), intro);
    title->setStyleSheet(QStringLiteral("color:#f4f8ff; font-size:24px; font-weight:700;"));
    introLayout->addWidget(title,0,Qt::AlignHCenter);
    auto* description = bodyText(QStringLiteral("Connect a USB storage device to begin inspecting its details and scan results."), intro);
    description->setAlignment(Qt::AlignCenter);
    introLayout->addWidget(description);
    auto* detect = new QPushButton(QStringLiteral("Scan for Devices"), intro);
    detect->setMinimumHeight(44);
    detect->setMaximumWidth(230);
    detect->setCursor(Qt::PointingHandCursor);
    detect->setStyleSheet(QStringLiteral("QPushButton { color:white; background:#1677c8; border:0; border-radius:9px; padding:10px 18px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:focus { border:2px solid #72dafa; }"));
    introLayout->addWidget(detect,0,Qt::AlignHCenter);
    auto* note = bodyText(QStringLiteral("Device detection only identifies connected devices. It does not modify their contents."), intro);
    note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet(QStringLiteral("color:#8fa7c1; font-size:11px;"));
    introLayout->addWidget(note);
    layout->addWidget(intro);

    auto* listCard = makeCard(page);
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(20,16,20,16);
    listLayout->addWidget(heading(QStringLiteral("Connected devices"), listCard));
    auto* list = new QFrame(listCard);
    list->setMinimumHeight(96);
    list->setStyleSheet(QStringLiteral("QFrame { background:rgba(2,11,29,100); border:1px dashed rgba(140,179,211,90); border-radius:10px; }"));
    auto* listBox = new QVBoxLayout(list);
    auto* empty = bodyText(QStringLiteral("No devices listed. Device detection is not connected yet."), list);
    empty->setAlignment(Qt::AlignCenter);
    listBox->addWidget(empty);
    listLayout->addWidget(list);
    layout->addWidget(listCard);
    layout->addStretch();

    connect(detect, &QPushButton::clicked, page, [this]() {
        QMessageBox::information(this, QStringLiteral("Device detection unavailable"), QStringLiteral("Device detection is not connected yet."));
    });
    return page;
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
