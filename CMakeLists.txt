cmake_minimum_required(VERSION 3.21)
project(Lexicon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

option(LEXICON_BUILD_DESKTOP "Build the Qt desktop client" ON)
option(LEXICON_BUILD_SERVER "Build the Qt-free REST server" ON)
include(CTest)

find_package(SQLite3 3.38 REQUIRED)
if (LEXICON_BUILD_SERVER)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)
    find_package(Threads REQUIRED)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()

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
    lexicon-core/ImageValue.h
    lexicon-core/ImageValue.cpp
    lexicon-core/Review.h
    lexicon-core/Utf8Path.h
    lexicon-core/Utf8Path.cpp
    lexicon-core/Validation.h
    lexicon-core/Validation.cpp
    lexicon-core/WikiLinks.h
    lexicon-core/WikiLinks.cpp
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
    lexicon-storage-sqlite/SearchIndex.cpp
)
target_include_directories(lexicon-storage-sqlite PUBLIC lexicon-storage-sqlite)
target_link_libraries(lexicon-storage-sqlite PUBLIC lexicon-application PRIVATE SQLite::SQLite3 OpenSSL::Crypto)
set_target_properties(lexicon-storage-sqlite PROPERTIES AUTOMOC OFF AUTORCC OFF)

# Vendored, pinned, header-only third-party libraries. Normal builds never
# download anything.
add_library(lexicon-nlohmann-json INTERFACE)
target_include_directories(lexicon-nlohmann-json SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nlohmann-json/include)

# The JSON representation of Lexicon records, shared by the REST server and the
# export file format, which the desktop client reads and writes too. Qt-free.
add_library(lexicon-json STATIC
    lexicon-json/Transport.h
    lexicon-json/Transport.cpp
    lexicon-json/Exchange.h
    lexicon-json/Exchange.cpp
)
target_include_directories(lexicon-json PUBLIC lexicon-json)
# Transport.h exposes nlohmann::json, so consumers need its include path.
target_link_libraries(lexicon-json PUBLIC lexicon-application lexicon-nlohmann-json)
set_target_properties(lexicon-json PROPERTIES AUTOMOC OFF AUTORCC OFF)

if (LEXICON_BUILD_SERVER)
    add_library(lexicon-cpp-httplib INTERFACE)
    target_include_directories(lexicon-cpp-httplib SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cpp-httplib)
    # Every translation unit including httplib.h must agree on the TLS layout.
    target_compile_definitions(lexicon-cpp-httplib INTERFACE
        CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(lexicon-cpp-httplib INTERFACE
        OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    if (WIN32)
        # Sockets, and the certificate store OpenSSL reaches for on Windows.
        target_link_libraries(lexicon-cpp-httplib INTERFACE ws2_32 crypt32)
    endif()

    add_library(lexicon-http STATIC
        lexicon-http/FilePath.h
        lexicon-http/FilePath.cpp
        lexicon-http/Security.h
        lexicon-http/Security.cpp
        lexicon-http/AuthState.h
        lexicon-http/AuthState.cpp
        lexicon-http/ServerConfig.h
        lexicon-http/ServerConfig.cpp
        lexicon-http/TempFile.h
        lexicon-http/TempFile.cpp
        lexicon-http/RestServer.h
        lexicon-http/RestServer.cpp
    )
    target_include_directories(lexicon-http PUBLIC lexicon-http)
    target_link_libraries(lexicon-http
        PUBLIC lexicon-application lexicon-json
        PRIVATE lexicon-cpp-httplib OpenSSL::Crypto)
    if (WIN32)
        # SetEntriesInAclW and SetNamedSecurityInfoW build the owner-only DACL
        # that stands in for POSIX mode 0600.
        target_link_libraries(lexicon-http PRIVATE advapi32)
    endif()
    set_target_properties(lexicon-http PROPERTIES AUTOMOC OFF AUTORCC OFF)

    add_library(lexicon-backup STATIC
        lexicon-server/Backup.h
        lexicon-server/Backup.cpp
    )
    target_include_directories(lexicon-backup PUBLIC lexicon-server)
    target_link_libraries(lexicon-backup PUBLIC lexicon-application lexicon-storage-sqlite lexicon-json Threads::Threads)
    set_target_properties(lexicon-backup PROPERTIES AUTOMOC OFF AUTORCC OFF)

    add_executable(LexiconServer lexicon-server/main.cpp)
    target_link_libraries(LexiconServer PRIVATE lexicon-http lexicon-storage-sqlite lexicon-backup)
    set_target_properties(LexiconServer PROPERTIES AUTOMOC OFF AUTORCC OFF)

    if (BUILD_TESTING)
        # Requests travel over a real socket, so the tests exercise routing,
        # headers, limits and status codes, not just handler functions.
        add_library(lexicon-server-test-support STATIC
            tests/support/HttpTestClient.h
            tests/support/HttpTestClient.cpp
            tests/support/ServerHarness.h
            tests/support/ServerHarness.cpp
        )
        target_include_directories(lexicon-server-test-support PUBLIC tests)
        target_link_libraries(lexicon-server-test-support
            PUBLIC lexicon-http lexicon-storage-sqlite
            PRIVATE lexicon-cpp-httplib OpenSSL::Crypto)
        set_target_properties(lexicon-server-test-support PROPERTIES
            AUTOMOC OFF AUTORCC OFF)

        add_executable(lexicon-backup-test tests/Backup.cpp)
        target_link_libraries(lexicon-backup-test PRIVATE lexicon-backup lexicon-http)
        set_target_properties(lexicon-backup-test PROPERTIES AUTOMOC OFF AUTORCC OFF)
        add_test(NAME backup COMMAND lexicon-backup-test)

        foreach(suite server-auth:ServerAuth:server_auth
                      server-rest:ServerRest:server_rest
                      server-concurrency:ServerConcurrency:server_concurrency)
            string(REPLACE ":" ";" parts ${suite})
            list(GET parts 0 suite_target)
            list(GET parts 1 suite_source)
            list(GET parts 2 suite_test)
            add_executable(lexicon-${suite_target} tests/${suite_source}.cpp)
            target_link_libraries(lexicon-${suite_target}
                PRIVATE lexicon-server-test-support)
            set_target_properties(lexicon-${suite_target} PROPERTIES
                AUTOMOC OFF AUTORCC OFF)
            add_test(NAME ${suite_test} COMMAND lexicon-${suite_target})
        endforeach()
    endif()
endif()

# The backend must remain usable when Qt is not installed.
set(LEXICON_QT_FREE_TARGETS lexicon-core lexicon-application lexicon-storage-sqlite lexicon-json)
if (LEXICON_BUILD_SERVER)
    list(APPEND LEXICON_QT_FREE_TARGETS lexicon-http lexicon-backup LexiconServer)
endif()
foreach(target ${LEXICON_QT_FREE_TARGETS})
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

    # md4qt and the converter on top of it, shared by the desktop client and
    # its GUI tests so md4qt is compiled once.
    add_library(lexicon-markdown STATIC
        lexicon-qt/MarkdownConverter.h
        lexicon-qt/MarkdownConverter.cpp
        ${MD4QT_SRC}
    )
    target_include_directories(lexicon-markdown PUBLIC 3rdparty/md4qt/src lexicon-qt)
    target_link_libraries(lexicon-markdown PUBLIC lexicon-core Qt${QT_VERSION_MAJOR}::Core)

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
        lexicon-qt/ReviewDialog.h
        lexicon-qt/ReviewDialog.cpp
        lexicon-qt/GraphLayout.h
        lexicon-qt/GraphDialog.h
        lexicon-qt/GraphDialog.cpp
        lexicon-qt/InboxDialog.h
        lexicon-qt/InboxDialog.cpp
        lexicon-qt/AlarmsDialog.h
        lexicon-qt/AlarmsDialog.cpp
        lexicon-qt/AlarmNotifier.h
        lexicon-qt/AlarmNotifier.cpp
        lexicon-qt/ImageValueView.h
        lexicon-qt/ImageValueView.cpp
        lexicon-qt/CardsDialog.h
        lexicon-qt/CardsDialog.cpp
        lexicon-qt/CardQuizDialog.h
        lexicon-qt/CardQuizDialog.cpp
    )

    target_link_libraries(Lexicon PRIVATE
        lexicon-application
        lexicon-storage-sqlite
        lexicon-json
        lexicon-qt-bridge
        lexicon-markdown
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

        # The desktop dialogs added since, driven offscreen.
        add_executable(lexicon-desktop-gui-smoke
            tests/DesktopGuiSmoke.cpp
            lexicon-qt/ApplicationContext.cpp
            lexicon-qt/ReviewDialog.h
            lexicon-qt/ReviewDialog.cpp
            lexicon-qt/ItemEditDialog.h
            lexicon-qt/ItemEditDialog.cpp
            lexicon-qt/GraphLayout.h
            lexicon-qt/GraphDialog.h
            lexicon-qt/GraphDialog.cpp
            lexicon-qt/InboxDialog.h
            lexicon-qt/InboxDialog.cpp
            lexicon-qt/AlarmsDialog.h
            lexicon-qt/AlarmsDialog.cpp
            lexicon-qt/AlarmNotifier.h
            lexicon-qt/AlarmNotifier.cpp
            lexicon-qt/ImageValueView.h
            lexicon-qt/ImageValueView.cpp
            lexicon-qt/CardsDialog.h
            lexicon-qt/CardsDialog.cpp
            lexicon-qt/CardQuizDialog.h
            lexicon-qt/CardQuizDialog.cpp
        )
        target_include_directories(lexicon-desktop-gui-smoke PRIVATE lexicon-qt)
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
        target_link_libraries(lexicon-desktop-gui-smoke PRIVATE
            lexicon-application lexicon-storage-sqlite lexicon-json lexicon-qt-bridge
            lexicon-markdown Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME desktop_gui_smoke COMMAND lexicon-desktop-gui-smoke)
        set_tests_properties(desktop_gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

if (BUILD_TESTING)
    add_executable(lexicon-core-smoke tests/CoreSmoke.cpp)
    target_link_libraries(lexicon-core-smoke PRIVATE lexicon-application)
    set_target_properties(lexicon-core-smoke PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME core_smoke COMMAND lexicon-core-smoke)

    add_executable(lexicon-wiki-links tests/WikiLinks.cpp)
    target_link_libraries(lexicon-wiki-links PRIVATE lexicon-core)
    set_target_properties(lexicon-wiki-links PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME wiki_links COMMAND lexicon-wiki-links)

    add_executable(lexicon-image-value tests/ImageValue.cpp)
    target_link_libraries(lexicon-image-value PRIVATE lexicon-core)
    set_target_properties(lexicon-image-value PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME image_value COMMAND lexicon-image-value)

    add_executable(lexicon-application-integration tests/ApplicationIntegration.cpp)
    target_link_libraries(lexicon-application-integration PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-application-integration PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME application_integration COMMAND lexicon-application-integration)

    add_executable(lexicon-sqlite-compatibility tests/SqliteCompatibility.cpp)
    target_link_libraries(lexicon-sqlite-compatibility PRIVATE lexicon-application lexicon-storage-sqlite SQLite::SQLite3)
    target_compile_definitions(lexicon-sqlite-compatibility PRIVATE
        LEXICON_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_target_properties(lexicon-sqlite-compatibility PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME sqlite_compatibility COMMAND lexicon-sqlite-compatibility)

    add_executable(lexicon-repository-correctness tests/RepositoryCorrectness.cpp)
    target_link_libraries(lexicon-repository-correctness PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-repository-correctness PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME repository_correctness COMMAND lexicon-repository-correctness)

    add_executable(lexicon-search tests/Search.cpp)
    # SQLite directly as well: the test edits the database behind the
    # repository's back, as another program would.
    target_link_libraries(lexicon-search PRIVATE lexicon-application lexicon-storage-sqlite SQLite::SQLite3)
    set_target_properties(lexicon-search PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME search COMMAND lexicon-search)

    add_executable(lexicon-review tests/Review.cpp)
    target_link_libraries(lexicon-review PRIVATE lexicon-application lexicon-storage-sqlite SQLite::SQLite3)
    set_target_properties(lexicon-review PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME review COMMAND lexicon-review)

    add_executable(lexicon-cards tests/Cards.cpp)
    # SQLite directly as well, to see what the database itself refuses; and
    # threads, for answers given on several connections at once.
    find_package(Threads REQUIRED)
    target_link_libraries(lexicon-cards PRIVATE lexicon-application lexicon-storage-sqlite SQLite::SQLite3 Threads::Threads)
    set_target_properties(lexicon-cards PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME cards COMMAND lexicon-cards)

    add_executable(lexicon-exchange tests/Exchange.cpp)
    target_link_libraries(lexicon-exchange PRIVATE lexicon-json lexicon-storage-sqlite)
    set_target_properties(lexicon-exchange PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME exchange COMMAND lexicon-exchange)

    add_executable(lexicon-blob-maintenance tests/BlobMaintenance.cpp)
    target_link_libraries(lexicon-blob-maintenance PRIVATE lexicon-application lexicon-storage-sqlite)
    set_target_properties(lexicon-blob-maintenance PROPERTIES AUTOMOC OFF AUTORCC OFF)
    add_test(NAME blob_maintenance COMMAND lexicon-blob-maintenance)
endif()
