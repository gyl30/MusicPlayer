cmake_minimum_required(VERSION 3.16)
project(MusicPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer (ASan) and ThreadSanitizer (TSan) cannot be enabled at the same time.")
endif()

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    message(STATUS "AddressSanititizer (ASan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer (TSan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer (UBSan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=undefined -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
endif()

set(HARDENING_FLAGS_COMMON
    -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough
    -Werror=format-security
    -fstack-clash-protection -fstack-protector-strong
)

if(UNIX AND NOT APPLE)
    list(APPEND HARDENING_FLAGS_COMMON
        -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
        -fPIE
    )
endif()

list(APPEND HARDENING_FLAGS_COMMON -D_GLIBCXX_ASSERTIONS)

# Initialize the variable empty first.
set(HARDENING_LINKER_FLAGS "")

# Apply linker flags ONLY on Linux.
if(UNIX AND NOT APPLE)
    list(APPEND HARDENING_LINKER_FLAGS
        -pie
        -Wl,-z,noexecstack
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,--as-needed
        -Wl,-z,nodlopen
        -Wl,--no-copy-dt-needed-entries
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "GCC detected, adding GCC-specific hardening flags.")
    list(APPEND HARDENING_FLAGS_COMMON -Wtrampolines)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
        message(STATUS "GCC >= 12, enabling -Wbidi-chars")
        list(APPEND HARDENING_FLAGS_COMMON -Wbidi-chars=any)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)
        message(STATUS "GCC >= 13, enabling -fstrict-flex-arrays=3")
        list(APPEND HARDENING_FLAGS_COMMON -fstrict-flex-arrays=3)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
        message(STATUS "GCC >= 15, enabling -fzero-init-padding-bits=all")
        list(APPEND HARDENING_FLAGS_COMMON -fzero-init-padding-bits=all)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Clang detected, most hardening flags are compatible.")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(STATUS "x86_64 architecture detected, enabling Control-Flow Protection.")
    list(APPEND HARDENING_FLAGS_COMMON -fcf-protection=full)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "aarch64 architecture detected, enabling Branch Protection.")
    list(APPEND HARDENING_FLAGS_COMMON -mbranch-protection=standard)
endif()

set(HARDENING_FLAGS_PRODUCTION
    -fno-delete-null-pointer-checks
    -fno-strict-overflow
    -fno-strict-aliasing
    -ftrivial-auto-var-init=zero
)

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
    playback_controller.cpp
    playlist_manager.cpp
    spectrum_processor.cpp
    spectrum_widget.cpp
    audio_decoder.cpp
    audio_player.cpp
)

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

if(WIN32)
    set_target_properties(MusicPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE)
    set_target_properties(MusicPlayer PROPERTIES MACOSX_BUNDLE TRUE)
endif()
