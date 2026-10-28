#include "AuthWindow.h"
#include "ImageBackgroundWidget.h"

#include <QApplication>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

namespace {
class BusyCursorGuard {
public:
    BusyCursorGuard() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
    ~BusyCursorGuard() { QGuiApplication::restoreOverrideCursor(); }
    BusyCursorGuard(const BusyCursorGuard&) = delete;
    BusyCursorGuard& operator=(const BusyCursorGuard&) = delete;
};

QLineEdit* makeInput(const QString& placeholder, QWidget* parent, bool password = false) {
    auto* input = new QLineEdit(parent); input->setPlaceholderText(placeholder); input->setMinimumHeight(46);
    if (password) { input->setEchoMode(QLineEdit::Password); input->setClearButtonEnabled(false); }
    input->setStyleSheet("QLineEdit { background:#fff; border:1px solid #b7e8f1; border-radius:9px; padding:0 14px; color:#0B2340; font-size:14px; } QLineEdit:focus { border:2px solid #1677C8; }");
    return input;
}

QWidget* passwordRow(QLineEdit* input, QWidget* parent) {
    auto* row = new QWidget(parent); auto* layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0); layout->setSpacing(8); layout->addWidget(input);
    auto* toggle = new QPushButton(QStringLiteral("Show"), row); toggle->setCheckable(true); toggle->setFixedWidth(62);
    toggle->setStyleSheet("QPushButton { border:0; color:#1677C8; font-weight:600; } QPushButton:focus { border:1px solid #1677C8; }");
    QObject::connect(toggle, &QPushButton::toggled, input, [input,toggle](bool show) { input->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password); toggle->setText(show ? QStringLiteral("Hide") : QStringLiteral("Show")); });
    layout->addWidget(toggle); return row;
}
}

AuthWindow::AuthWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("DataGuard — Secure. Recover. Verify."));
    setMinimumSize(760, 600); resize(1000, 660);
    if (m_database.initialize(&m_startupError)) m_auth = new AuthService(m_database.database());
    buildUi();
}

bool AuthWindow::isReady() const { return m_auth != nullptr; }
QString AuthWindow::startupError() const { return m_startupError; }

void AuthWindow::buildUi() {
    auto* background = new ImageBackgroundWidget(this); background->setObjectName(QStringLiteral("authBackground"));
    auto* outer = new QVBoxLayout(background); outer->setContentsMargins(24,20,24,20); outer->addStretch();
    auto* panel = new QWidget(background); panel->setObjectName(QStringLiteral("authPanel")); panel->setMinimumSize(0,470); panel->setMaximumWidth(920);
    auto* shadow = new QGraphicsDropShadowEffect(panel); shadow->setBlurRadius(32); shadow->setOffset(0,12); shadow->setColor(QColor(4,53,91,75)); panel->setGraphicsEffect(shadow);
    auto* panelLayout = new QVBoxLayout(panel); panelLayout->setContentsMargins(0,0,0,0); panelLayout->setSpacing(0);
    auto* chrome = new QWidget(panel); chrome->setFixedHeight(34); chrome->setStyleSheet("background:#07152f; border-top-left-radius:14px; border-top-right-radius:14px;");
    auto* dots = new QHBoxLayout(chrome); dots->setContentsMargins(14,0,0,0); dots->setSpacing(7);
    for (const QColor color : {QColor("#ff6b83"),QColor("#f2b7c4"),QColor("#7891b5")}) { auto* dot=new QLabel(chrome); dot->setFixedSize(10,10); dot->setStyleSheet(QString("background:%1; border-radius:5px;").arg(color.name())); dots->addWidget(dot); }
    dots->addStretch(); panelLayout->addWidget(chrome);
    auto* body = new QWidget(panel); body->setObjectName(QStringLiteral("authBody")); body->setStyleSheet("QWidget#authBody { background-color:rgba(4,16,42,226); border-bottom-left-radius:14px; border-bottom-right-radius:14px; }");
    auto* bodyLayout = new QHBoxLayout(body); bodyLayout->setContentsMargins(34,22,34,28); bodyLayout->setSpacing(28);
    auto* formArea = new QWidget(body); formArea->setMinimumWidth(300); auto* formLayout = new QVBoxLayout(formArea); formLayout->setContentsMargins(0,0,0,0); formLayout->setSpacing(11);
    auto* brand = new QLabel(QStringLiteral("◈  DataGuard"), formArea); brand->setStyleSheet("font-size:18px; font-weight:700; color:#ffffff;"); formLayout->addWidget(brand);
    auto* tagline = new QLabel(QStringLiteral("Secure. Recover. Verify."), formArea); tagline->setStyleSheet("color:#a7bbd3; font-size:12px; letter-spacing:1px;"); formLayout->addWidget(tagline);
    m_pages = new QStackedWidget(formArea); formLayout->addWidget(m_pages,1);

    auto* loginPage = new QWidget(m_pages); auto* login = new QVBoxLayout(loginPage); login->setContentsMargins(0,14,0,0); login->setSpacing(11);
    auto* loginTitle = new QLabel(QStringLiteral("Welcome Back!"), loginPage); loginTitle->setStyleSheet("font-size:26px; font-weight:700; color:#f6f8ff;"); login->addWidget(loginTitle);
    auto* loginIntro = new QLabel(QStringLiteral("Your data. Your control. Your security."), loginPage); loginIntro->setWordWrap(true); loginIntro->setStyleSheet("color:#afbdd0; font-size:13px;"); login->addWidget(loginIntro);
    m_identity=makeInput(QStringLiteral("Email or username"),loginPage); login->addWidget(m_identity);
    m_signInPassword=makeInput(QStringLiteral("Password"),loginPage,true); login->addWidget(passwordRow(m_signInPassword,loginPage));
    m_signInError=new QLabel(loginPage); m_signInError->setWordWrap(true); m_signInError->setStyleSheet("color:#ff9da9; font-size:12px;"); login->addWidget(m_signInError);
    m_signInButton=new QPushButton(QStringLiteral("Sign In"),loginPage); m_signInButton->setMinimumHeight(46); m_signInButton->setStyleSheet("QPushButton { background:#1677C8; color:white; border:0; border-radius:9px; font-size:15px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:disabled { background:#506d8a; }"); login->addWidget(m_signInButton);
    auto* links=new QHBoxLayout(); auto* noAccount=new QLabel(QStringLiteral("New to DataGuard?"),loginPage); noAccount->setStyleSheet("color:#afbdd0; font-size:12px;"); auto* createLink=new QPushButton(QStringLiteral("Create Account"),loginPage); createLink->setFlat(true); createLink->setStyleSheet("color:#72dafa; font-weight:700;"); links->addWidget(noAccount); links->addWidget(createLink); links->addStretch(); login->addLayout(links);
    auto* forgot=new QPushButton(QStringLiteral("Password recovery is not configured"),loginPage); forgot->setEnabled(false); forgot->setFlat(true); forgot->setStyleSheet("color:#8293aa; font-size:11px; text-align:left;"); login->addWidget(forgot); login->addStretch();
    m_pages->addWidget(loginPage);

    auto* registerPage=new QWidget(m_pages); auto* registration=new QVBoxLayout(registerPage); registration->setContentsMargins(0,12,0,0); registration->setSpacing(8);
    auto* registerTitle=new QLabel(QStringLiteral("Create Your Account"),registerPage); registerTitle->setStyleSheet("font-size:24px; font-weight:700; color:#f6f8ff;"); registration->addWidget(registerTitle);
    auto* registerIntro=new QLabel(QStringLiteral("Start managing your data securely."),registerPage); registerIntro->setStyleSheet("color:#afbdd0; font-size:13px;"); registration->addWidget(registerIntro);
    m_username=makeInput(QStringLiteral("Username (3–32 letters, numbers, _)"),registerPage); registration->addWidget(m_username);
    m_email=makeInput(QStringLiteral("Email address"),registerPage); registration->addWidget(m_email);
    m_password=makeInput(QStringLiteral("Password (at least 12 characters)"),registerPage,true); registration->addWidget(passwordRow(m_password,registerPage));
    m_confirm=makeInput(QStringLiteral("Confirm password"),registerPage,true); registration->addWidget(passwordRow(m_confirm,registerPage));
    m_registerError=new QLabel(registerPage); m_registerError->setWordWrap(true); m_registerError->setStyleSheet("color:#ff9da9; font-size:12px;"); registration->addWidget(m_registerError);
    m_createButton=new QPushButton(QStringLiteral("Create Account"),registerPage); m_createButton->setMinimumHeight(44); m_createButton->setStyleSheet("QPushButton { background:#1677C8; color:white; border:0; border-radius:9px; font-size:14px; font-weight:700; } QPushButton:hover { background:#2395dc; } QPushButton:disabled { background:#506d8a; }"); registration->addWidget(m_createButton);
    auto* back=new QPushButton(QStringLiteral("Already have an account?  Sign In"),registerPage); back->setFlat(true); back->setStyleSheet("color:#72dafa; font-weight:600;"); registration->addWidget(back); registration->addStretch(); m_pages->addWidget(registerPage);
    connect(createLink,&QPushButton::clicked,this,&AuthWindow::showCreateAccount); connect(back,&QPushButton::clicked,this,&AuthWindow::showSignIn);
    connect(m_signInButton,&QPushButton::clicked,this,&AuthWindow::submitSignIn); connect(m_createButton,&QPushButton::clicked,this,&AuthWindow::submitRegistration);
    connect(m_identity,&QLineEdit::returnPressed,this,&AuthWindow::submitSignIn); connect(m_signInPassword,&QLineEdit::returnPressed,this,&AuthWindow::submitSignIn);
    connect(m_password,&QLineEdit::returnPressed,this,&AuthWindow::submitRegistration); connect(m_confirm,&QLineEdit::returnPressed,this,&AuthWindow::submitRegistration);

    auto* contextArea=new QWidget(body); auto* contextLayout=new QVBoxLayout(contextArea); contextLayout->setContentsMargins(16,0,4,0); contextLayout->addStretch();
    auto* contextTitle=new QLabel(QStringLiteral("Your workspace,\nunder your control."),contextArea); contextTitle->setWordWrap(true); contextTitle->setStyleSheet("font-size:24px; font-weight:700; color:#f6f8ff;"); contextLayout->addWidget(contextTitle);
    auto* contextText=new QLabel(QStringLiteral("Local accounts. No cloud sync. Manage your storage with clear, deliberate controls."),contextArea); contextText->setWordWrap(true); contextText->setStyleSheet("font-size:13px; line-height:1.5; color:#afbdd0;"); contextLayout->addWidget(contextText); contextLayout->addStretch();
    bodyLayout->addWidget(formArea,5); bodyLayout->addWidget(contextArea,4); panelLayout->addWidget(body,1);
    outer->addWidget(panel,1); outer->addStretch(); setCentralWidget(background);
    if (!isReady()) { m_signInButton->setEnabled(false); m_createButton->setEnabled(false); m_signInError->setText(QStringLiteral("Local account database unavailable: %1").arg(m_startupError)); }
}

void AuthWindow::showSignIn() { m_pages->setCurrentIndex(0); m_signInError->clear(); m_identity->setFocus(); }
void AuthWindow::showCreateAccount() { m_pages->setCurrentIndex(1); m_registerError->clear(); m_username->setFocus(); }
void AuthWindow::submitSignIn() {
    m_signInError->clear(); if (m_identity->text().trimmed().isEmpty() || m_signInPassword->text().isEmpty()) { m_signInError->setText(QStringLiteral("Enter your email/username and password.")); return; }
    m_signInButton->setEnabled(false); m_createButton->setEnabled(false); m_signInButton->setText(QStringLiteral("Signing in…"));
    QString username,error;
    bool ok = false;
    {
        BusyCursorGuard busyCursor;
        ok=m_auth && m_auth->signIn(m_identity->text(),m_signInPassword->text(),&username,&error);
    }
    m_signInPassword->clear(); m_signInButton->setEnabled(true); m_createButton->setEnabled(true); m_signInButton->setText(QStringLiteral("Sign In"));
    if (!ok) { m_signInError->setText(error.isEmpty()?QStringLiteral("Sign in is temporarily unavailable."):error); return; }
    emit authenticated(username);
}
void AuthWindow::submitRegistration() {
    m_registerError->clear();
    if (m_username->text().trimmed().isEmpty() || m_email->text().trimmed().isEmpty() || m_password->text().isEmpty() || m_confirm->text().isEmpty()) { m_registerError->setText(QStringLiteral("Complete every field to create your account.")); return; }
    if (m_password->text()!=m_confirm->text()) { m_registerError->setText(QStringLiteral("Passwords do not match.")); return; }
    m_signInButton->setEnabled(false); m_createButton->setEnabled(false); m_createButton->setText(QStringLiteral("Creating account…"));
    QString error;
    bool ok = false;
    {
        BusyCursorGuard busyCursor;
        ok=m_auth && m_auth->registerUser(m_username->text(),m_email->text(),m_password->text(),&error);
    }
    m_password->clear(); m_confirm->clear(); m_signInButton->setEnabled(true); m_createButton->setEnabled(true); m_createButton->setText(QStringLiteral("Create Account"));
    if (!ok) { m_registerError->setText(error.isEmpty()?QStringLiteral("Account creation is temporarily unavailable."):error); return; }
    QMessageBox::information(this,QStringLiteral("Account created"),QStringLiteral("Your local DataGuard account is ready. Sign in to continue."));
    m_identity->setText(m_email->text().trimmed().toLower()); showSignIn();
}
