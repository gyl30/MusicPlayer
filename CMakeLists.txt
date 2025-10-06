cmake_minimum_required(VERSION 3.16)
project(QtFfmpegPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswresample)

add_executable(player
    main.cpp
    log.cpp
    mainwindow.cpp
    spectrum_widget.cpp
    audio_decoder_thread.cpp
)

target_include_directories(player PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    third/spdlog/include
)

target_link_directories(player PRIVATE
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(player PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    ${FFMPEG_LIBRARIES}
)

set_target_properties(player PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
