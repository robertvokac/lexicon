cmake_minimum_required(VERSION 3.21)
project(Lexicon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql)

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
    src/MarkdownConverter.h
    src/MarkdownConverter.cpp
    ${MD4QT_SRC}
)

target_include_directories(Lexicon PRIVATE 3rdparty/md4qt/src)

target_link_libraries(Lexicon PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
)

if (WIN32)
    set_target_properties(Lexicon PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
