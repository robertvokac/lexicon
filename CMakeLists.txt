cmake_minimum_required(VERSION 3.21)
project(Lexicon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

option(LEXICON_BUILD_DESKTOP "Build the Qt desktop client" ON)
include(CTest)

find_package(SQLite3 3.38 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(MD4QT_SRC
    3rdparty/md4qt/src/algo.cpp
    3rdparty/md4qt/src/asterisk_emphasis_parser.cpp
    3rdparty/md4qt/src/atx_heading_parser.cpp
    3rdparty/md4qt/src/autolink_parser.cpp
    3rdparty/md4qt/src/block_parser.cpp
    3rdparty/md4qt/src/blockquote_parser.cpp
    3rdparty/md4qt/src/context.cpp
    3rdparty/md4qt/src/doc.cpp
    3rdparty/md4qt/src/emphasis_parser.cpp
    3rdparty/md4qt/src/entities_map.cpp
    3rdparty/md4qt/src/fenced_code_parser.cpp
    3rdparty/md4qt/src/footnote_parser.cpp
    3rdparty/md4qt/src/gfm_autolink_parser.cpp
    3rdparty/md4qt/src/hard_line_break_parser.cpp
    3rdparty/md4qt/src/html.cpp
    3rdparty/md4qt/src/html_parser.cpp
    3rdparty/md4qt/src/indented_code_parser.cpp
    3rdparty/md4qt/src/inline_code_parser.cpp
    3rdparty/md4qt/src/inline_context.cpp
    3rdparty/md4qt/src/inline_html_parser.cpp
    3rdparty/md4qt/src/inline_math_parser.cpp
    3rdparty/md4qt/src/inline_parser.cpp
    3rdparty/md4qt/src/link_image_parser.cpp
    3rdparty/md4qt/src/list_parser.cpp
    3rdparty/md4qt/src/paragraph_parser.cpp
    3rdparty/md4qt/src/parser.cpp
    3rdparty/md4qt/src/poscache.cpp
    3rdparty/md4qt/src/reverse_solidus.cpp
    3rdparty/md4qt/src/setext_heading_parser.cpp
    3rdparty/md4qt/src/strikethrough_emphasis_parser.cpp
    3rdparty/md4qt/src/table_parser.cpp
    3rdparty/md4qt/src/text_stream.cpp
    3rdparty/md4qt/src/thematic_break_parser.cpp
    3rdparty/md4qt/src/underline_emphasis_parser.cpp
    3rdparty/md4qt/src/utils.cpp
    3rdparty/md4qt/src/visitor.cpp
    3rdparty/md4qt/src/yaml_parser.cpp
)

add_library(lexicon-core STATIC
    lexicon-core/Records.h
    lexicon-core/Result.h
    lexicon-core/Validation.h
    lexicon-core/Validation.cpp
)
target_include_directories(lexicon-core PUBLIC lexicon-core)
set_target_properties(lexicon-core PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_library(lexicon-application STATIC
    lexicon-application/BlobMaintenance.h
    lexicon-application/Repository.h
    lexicon-application/LexiconApplication.h
    lexicon-application/LexiconApplication.cpp
)
target_include_directories(lexicon-application PUBLIC lexicon-application)
target_link_libraries(lexicon-application PUBLIC lexicon-core)
set_target_properties(lexicon-application PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_library(lexicon-storage-sqlite STATIC
    lexicon-storage-sqlite/SqliteRepository.h
    lexicon-storage-sqlite/SqliteRepository.cpp
    lexicon-storage-sqlite/SqliteInternal.h
    lexicon-storage-sqlite/Migrations.cpp
)
target_include_directories(lexicon-storage-sqlite PUBLIC lexicon-storage-sqlite)
target_link_libraries(lexicon-storage-sqlite PUBLIC lexicon-application PRIVATE SQLite::SQLite3 OpenSSL::Crypto)
set_target_properties(lexicon-storage-sqlite PROPERTIES AUTOMOC OFF AUTORCC OFF)

# The backend must remain usable when Qt is not installed.
foreach(target lexicon-core lexicon-application lexicon-storage-sqlite)
    get_target_property(direct_links ${target} LINK_LIBRARIES)
    get_target_property(public_links ${target} INTERFACE_LINK_LIBRARIES)
    if ("${direct_links};${public_links}" MATCHES "Qt[56]?::")
        message(FATAL_ERROR "${target} must not link Qt")
    endif()
endforeach()

if (LEXICON_BUILD_DESKTOP)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

    add_library(lexicon-qt-bridge STATIC
        lexicon-qt-bridge/QtRecords.h
        lexicon-qt-bridge/Conversions.h
        lexicon-qt-bridge/Conversions.cpp
    )
    target_include_directories(lexicon-qt-bridge PUBLIC lexicon-qt-bridge)
    target_link_libraries(lexicon-qt-bridge PUBLIC lexicon-core Qt${QT_VERSION_MAJOR}::Core)

    add_executable(Lexicon
        lexicon-qt/main.cpp
        lexicon-qt/ApplicationContext.h
        lexicon-qt/ApplicationContext.cpp
        lexicon-qt/MainWindow.h
        lexicon-qt/MainWindow.cpp
        lexicon-qt/FilterHeaderView.h
        lexicon-qt/FilterHeaderView.cpp
        lexicon-qt/PropertyFilterDialog.h
        lexicon-qt/PropertyFilterDialog.cpp
        lexicon-qt/ItemEditDialog.h
        lexicon-qt/ItemEditDialog.cpp
        lexicon-qt/GroupManagerDialog.h
        lexicon-qt/GroupManagerDialog.cpp
        lexicon-qt/ItemTypeManagerDialog.h
        lexicon-qt/ItemTypeManagerDialog.cpp
        lexicon-qt/BlobMaintenanceDialog.h
        lexicon-qt/BlobMaintenanceDialog.cpp
        lexicon-qt/ValueListDialog.h
        lexicon-qt/ValueListDialog.cpp
        lexicon-qt/MarkdownConverter.h
        lexicon-qt/MarkdownConverter.cpp
        ${MD4QT_SRC}
    )

    target_include_directories(Lexicon PRIVATE 3rdparty/md4qt/src)
    target_link_libraries(Lexicon PRIVATE
        lexicon-application
        lexicon-storage-sqlite
        lexicon-qt-bridge
        Qt${QT_VERSION_MAJOR}::Widgets
    )

    if (WIN32)
        set_target_properties(Lexicon PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    if (BUILD_TESTING)
        add_executable(lexicon-blob-gui-smoke
            tests/BlobGuiSmoke.cpp
            lexicon-qt/BlobMaintenanceDialog.h
            lexicon-qt/BlobMaintenanceDialog.cpp
            lexicon-qt/ApplicationContext.cpp
        )
        target_include_directories(lexicon-blob-gui-smoke PRIVATE lexicon-qt)
        target_link_libraries(lexicon-blob-gui-smoke PRIVATE
            lexicon-application lexicon-storage-sqlite lexicon-qt-bridge
            Qt${QT_VERSION_MAJOR}::Widgets)
        add_test(NAME blob_gui_smoke COMMAND lexicon-blob-gui-smoke)
        set_tests_properties(blob_gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

if (BUILD_TESTING)
    add_executable(lexicon-core-smoke tests/CoreSmoke.cpp)
    target_link_libraries(lexicon-core-smoke PRIVATE lexicon-application)
    set_target_properties(lexicon-core-smoke PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME core_smoke COMMAND lexicon-core-smoke)

    add_executable(lexicon-application-integration tests/ApplicationIntegration.cpp)
    target_link_libraries(lexicon-application-integration PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-application-integration PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME application_integration COMMAND lexicon-application-integration)

    add_executable(lexicon-sqlite-compatibility tests/SqliteCompatibility.cpp)
    target_link_libraries(lexicon-sqlite-compatibility PRIVATE lexicon-application lexicon-storage-sqlite)
    target_compile_definitions(lexicon-sqlite-compatibility PRIVATE
        LEXICON_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_target_properties(lexicon-sqlite-compatibility PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME sqlite_compatibility COMMAND lexicon-sqlite-compatibility)

    add_executable(lexicon-repository-correctness tests/RepositoryCorrectness.cpp)
    target_link_libraries(lexicon-repository-correctness PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-repository-correctness PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME repository_correctness COMMAND lexicon-repository-correctness)

    add_executable(lexicon-blob-maintenance tests/BlobMaintenance.cpp)
    target_link_libraries(lexicon-blob-maintenance PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-blob-maintenance PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME blob_maintenance COMMAND lexicon-blob-maintenance)
endif()
