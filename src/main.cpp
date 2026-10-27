#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"
#include "AuthWindow.h"
#include <QMessageBox>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DataGuard");
    QCoreApplication::setApplicationName("DataGuard");

    Logger::instance().init("logs");   
    LOG_INFO("APP", "WipeEngine started");
 
    AuthWindow authWindow;
    if (!authWindow.isReady()) {
        QMessageBox::critical(nullptr, "DataGuard database error",
            QString("The local account database could not be initialized.\n\n%1").arg(authWindow.startupError()));
        return 1;
    }

    MainWindow window;
    QObject::connect(&authWindow, &AuthWindow::authenticated, &window, [&](const QString& username) {
        window.setSignedInUser(username);
        authWindow.hide();
        window.show();
    });
    QObject::connect(&window, &MainWindow::signOutRequested, &window, [&]() {
        window.hide();
        authWindow.show();
    });
    authWindow.show();
    return app.exec();
}

