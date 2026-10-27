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
    src/FileScanner.cpp
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
endif()
