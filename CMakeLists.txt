cmake_minimum_required(VERSION 3.16)
project(WipeEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

add_executable(WipeEngine WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/DeviceManager.cpp
    src/WipeEngine.cpp
    src/StorageSafety.cpp
    src/FileScanner.cpp
    src/Fat32Scanner.cpp
    src/Logger.cpp
    src/CertificateGenerator.cpp
    src/FeatureExtractor.cpp
    src/RandomForest.cpp
    src/AuthWindow.cpp
    src/AuthService.cpp
    src/DatabaseManager.cpp
    src/PasswordHasher.cpp
    src/ImageBackgroundWidget.cpp
    resources.qrc
    src/app.rc
)

target_link_libraries(WipeEngine PRIVATE Qt6::Widgets Qt6::Sql bcrypt)

include(CTest)
if(BUILD_TESTING)
    add_executable(AuthServiceTests
        tests/AuthServiceTests.cpp
        src/AuthService.cpp
        src/DatabaseManager.cpp
        src/PasswordHasher.cpp
    )
    target_link_libraries(AuthServiceTests PRIVATE Qt6::Sql bcrypt)
    add_test(NAME AuthServiceTests COMMAND AuthServiceTests)

    add_executable(DashboardStateTests
        tests/DashboardStateTests.cpp
        src/MainWindow.cpp
        src/DeviceManager.cpp
        src/Fat32Scanner.cpp
        src/ImageBackgroundWidget.cpp
        resources.qrc
    )
    target_link_libraries(DashboardStateTests PRIVATE Qt6::Widgets)
    add_test(NAME DashboardStateTests COMMAND DashboardStateTests)
    set_tests_properties(DashboardStateTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(WipeSafetyTests
        tests/WipeSafetyTests.cpp
        src/WipeEngine.cpp
        src/StorageSafety.cpp
    )
    target_link_libraries(WipeSafetyTests PRIVATE Qt6::Core)
    add_test(NAME WipeSafetyTests COMMAND WipeSafetyTests)

    add_executable(CertificateGeneratorTests
        tests/CertificateGeneratorTests.cpp
        src/CertificateGenerator.cpp
    )
    target_link_libraries(CertificateGeneratorTests PRIVATE Qt6::Core)
    add_test(NAME CertificateGeneratorTests COMMAND CertificateGeneratorTests)

    add_executable(FileScannerSafetyTests
        tests/FileScannerSafetyTests.cpp
        src/FileScanner.cpp
        src/Fat32Scanner.cpp
    )
    target_link_libraries(FileScannerSafetyTests PRIVATE Qt6::Core)
    add_test(NAME FileScannerSafetyTests COMMAND FileScannerSafetyTests)
endif()
