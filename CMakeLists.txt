cmake_minimum_required(VERSION 3.21)
project(MusicPlayer VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(WIN32)
    add_compile_definitions(SPDLOG_WCHAR_FILENAMES)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer (ASan) and ThreadSanitizer (TSan) cannot be enabled at the same time.")
endif()

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")
set(HARDENING_FLAGS_COMMON "")
set(HARDENING_FLAGS_PRODUCTION "")
set(HARDENING_LINKER_FLAGS "")

if(MSVC)
    list(APPEND HARDENING_FLAGS_COMMON /W4 /GS /sdl /guard:cf)
    list(APPEND HARDENING_LINKER_FLAGS /DYNAMICBASE /NXCOMPAT /guard:cf)
    if(ENABLE_TSAN OR ENABLE_UBSAN)
        message(FATAL_ERROR "MSVC 配置不支持 ENABLE_TSAN/ENABLE_UBSAN；请使用 GCC 或 Clang。")
    endif()
    if(ENABLE_ASAN)
        # MSVC 的 /RTC1 与 ASan 不兼容。
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        list(APPEND SANITIZER_COMPILE_FLAGS /fsanitize=address /Zi)
        list(APPEND SANITIZER_LINK_FLAGS /INCREMENTAL:NO)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    include(CheckCXXCompilerFlag)
    list(APPEND HARDENING_FLAGS_COMMON
        -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough -Werror=format-security
    )
    list(APPEND HARDENING_FLAGS_PRODUCTION
        -fno-delete-null-pointer-checks -fno-strict-overflow -fno-strict-aliasing
    )
    foreach(flag IN ITEMS -fstack-clash-protection -fstack-protector-strong)
        string(MAKE_C_IDENTIFIER "${flag}" flag_id)
        check_cxx_compiler_flag("${flag}" "MUSICPLAYER_HAS_${flag_id}")
        if(MUSICPLAYER_HAS_${flag_id})
            list(APPEND HARDENING_FLAGS_COMMON "${flag}")
        endif()
    endforeach()
    check_cxx_compiler_flag(-ftrivial-auto-var-init=zero MUSICPLAYER_HAS_AUTO_VAR_INIT)
    if(MUSICPLAYER_HAS_AUTO_VAR_INIT)
        list(APPEND HARDENING_FLAGS_PRODUCTION -ftrivial-auto-var-init=zero)
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        check_cxx_compiler_flag(-fcf-protection=full MUSICPLAYER_HAS_CF_PROTECTION)
        if(MUSICPLAYER_HAS_CF_PROTECTION)
            list(APPEND HARDENING_FLAGS_COMMON -fcf-protection=full)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        check_cxx_compiler_flag(-mbranch-protection=standard MUSICPLAYER_HAS_BRANCH_PROTECTION)
        if(MUSICPLAYER_HAS_BRANCH_PROTECTION)
            list(APPEND HARDENING_FLAGS_COMMON -mbranch-protection=standard)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND HARDENING_FLAGS_COMMON -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fPIE -D_GLIBCXX_ASSERTIONS)
        list(APPEND HARDENING_LINKER_FLAGS
            -pie -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now
            -Wl,--as-needed -Wl,-z,nodlopen -Wl,--no-copy-dt-needed-entries
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND HARDENING_FLAGS_COMMON -Wtrampolines)
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
            list(APPEND HARDENING_FLAGS_COMMON -Wbidi-chars=any)
        endif()
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)
            list(APPEND HARDENING_FLAGS_COMMON -fstrict-flex-arrays=3)
        endif()
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
            list(APPEND HARDENING_FLAGS_COMMON -fzero-init-padding-bits=all)
        endif()
    endif()

    if(ENABLE_ASAN)
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
    endif()
    if(ENABLE_TSAN)
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -g)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
    endif()
    if(ENABLE_UBSAN)
        list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=undefined -fno-omit-frame-pointer -g)
        list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
    endif()
elseif(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    message(FATAL_ERROR "当前编译器未配置 sanitizer 参数。")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Svg Sql)
find_package(SDL2 REQUIRED)
find_package(Boost REQUIRED)
if(WIN32)
    set(FFMPEG_ROOT "" CACHE PATH "FFmpeg 共享库安装目录（include、lib、bin）")
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH)
    if(NOT FFMPEG_INCLUDE_DIR)
        message(FATAL_ERROR "请用 FFMPEG_ROOT 指定 FFmpeg 5.1 或更新版本的共享库安装目录。")
    endif()
    add_library(FFmpeg INTERFACE)
    target_include_directories(FFmpeg SYSTEM INTERFACE "${FFMPEG_INCLUDE_DIR}")
    foreach(component avformat avcodec avutil swresample)
        find_library(FFMPEG_${component}_LIBRARY NAMES ${component} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        if(NOT FFMPEG_${component}_LIBRARY)
            message(FATAL_ERROR "FFMPEG_ROOT 中缺少 ${component} 导入库。")
        endif()
        target_link_libraries(FFmpeg INTERFACE "${FFMPEG_${component}_LIBRARY}")
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat>=59.27.100 libavcodec>=59.37.100 libavutil>=57.28.100 libswresample>=4.7.100
    )
    add_library(FFmpeg ALIAS PkgConfig::FFMPEG)
endif()

set(MUSICPLAYER_SOURCES
    log.cpp
    playback_controller.cpp
    playlist_manager.cpp
    database_manager.cpp
    duration_scanner.cpp
    playlist_window.cpp
    player_window.cpp
    scrolling_text_label.cpp
    audio_decoder.cpp
    lyrics_parser.cpp
    audio_player.cpp
    quick_editor.cpp
    volumemeter.cpp
    resources.qrc
    tray_icon.cpp
    music_management_dialog.cpp
    lyrics_widget.cpp
)

add_executable(MusicPlayer main.cpp ${MUSICPLAYER_SOURCES})

target_compile_options(MusicPlayer PRIVATE
    ${HARDENING_FLAGS_COMMON}
    $<$<CONFIG:Release,RelWithDebInfo>:${HARDENING_FLAGS_PRODUCTION}>
)
target_link_options(MusicPlayer PRIVATE
    ${HARDENING_LINKER_FLAGS}
)

if(SANITIZER_COMPILE_FLAGS)
    target_compile_options(MusicPlayer PRIVATE ${SANITIZER_COMPILE_FLAGS})
    target_link_options(MusicPlayer PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

target_include_directories(MusicPlayer SYSTEM PRIVATE
    ${SDL2_INCLUDE_DIRS}
    third/spdlog/include
)

target_link_libraries(MusicPlayer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Svg
    Qt6::Sql
    SDL2::SDL2
    Boost::boost
    FFmpeg
)

if(WIN32)
    set_target_properties(MusicPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(MusicPlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.gyl30.MusicPlayer
        MACOSX_BUNDLE_BUNDLE_NAME MusicPlayer
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

include(packaging/Packaging.cmake)

option(BUILD_TESTING "Build playback regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
    add_executable(playback_tests tests/playback_tests.cpp ${MUSICPLAYER_SOURCES})
    target_include_directories(playback_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(playback_tests SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS} third/spdlog/include)
    target_link_libraries(playback_tests PRIVATE Qt6::Test Qt6::Widgets Qt6::Multimedia Qt6::Svg Qt6::Sql SDL2::SDL2 Boost::boost FFmpeg)
    target_compile_definitions(playback_tests PRIVATE FFMPEG_EXECUTABLE="${FFMPEG_EXECUTABLE}")
    target_compile_options(playback_tests PRIVATE ${HARDENING_FLAGS_COMMON} ${SANITIZER_COMPILE_FLAGS})
    target_link_options(playback_tests PRIVATE ${SANITIZER_LINK_FLAGS})
    foreach(case unknown_duration latest_seek paused_seek exit_while_decoding rapid_switch formats invalid_files startup_seek callback_ownership stale_errors short_packets shuffle_history mode_boundaries remove_playlist remove_song refresh_nodes storage_atomicity storage_minimal_writes management_failure storage_write_failure playback_failure_limit shuffle_manual_selection async_import metadata_removed metadata_update_failure metadata_scan_shutdown metadata_path_reuse close_without_tray management_cancel ui_controls management_bulk_selection management_unchanged_return stale_ready_seek_callbacks library_views)
        add_test(NAME ${case} COMMAND playback_tests ${case})
        set_tests_properties(${case} PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy")
    endforeach()
endif()
