#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"

// Request administrator privileges automatically
#pragma comment(linker, "/manifestuac:level='requireAdministrator'")

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    Logger::instance().init("logs");   
    LOG_INFO("APP", "WipeEngine started");
 
    MainWindow window;
    window.show();
    return app.exec();
}

