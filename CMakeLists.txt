cmake_minimum_required(VERSION 3.16)
project(MusicPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswresample)

add_executable(MusicPlayer
    main.cpp
    log.cpp
    mainwindow.cpp
    spectrum_widget.cpp
    audio_decoder.cpp
)

target_include_directories(MusicPlayer PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    third/spdlog/include
)

target_link_directories(MusicPlayer PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(MusicPlayer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
)

set_target_properties(MusicPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
