cmake_minimum_required(VERSION 3.21)
project(Lexicon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

add_executable(Lexicon
    src/main.cpp
    src/DatabaseManager.h
    src/DatabaseManager.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TermEditDialog.h
    src/TermEditDialog.cpp
    src/MapManagerDialog.h
    src/MapManagerDialog.cpp
    src/ValueListDialog.h
    src/ValueListDialog.cpp
)

target_link_libraries(Lexicon PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
)

if (WIN32)
    set_target_properties(Lexicon PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
