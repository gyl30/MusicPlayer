include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME MusicPlayer)
set(CPACK_PACKAGE_VENDOR "MusicPlayer project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt 本地音频播放器")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gyl30/MusicPlayer")
if(NOT CPACK_PACKAGE_CONTACT)
    set(CPACK_PACKAGE_CONTACT "https://github.com/gyl30/MusicPlayer/issues")
endif()
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY MusicPlayer)

if(WIN32 OR APPLE)
    get_target_property(qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_executable}" DIRECTORY)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${qt_bin_dir}" NO_DEFAULT_PATH REQUIRED)
    install(TARGETS MusicPlayer
        RUNTIME_DEPENDENCIES
            DIRECTORIES "${FFMPEG_ROOT}/bin" "$<TARGET_FILE_DIR:SDL2::SDL2>" "${qt_bin_dir}"
            PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
            POST_EXCLUDE_REGEXES [[.*[/\\][Ss]ystem32[/\\].*]]
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/DeployWindows.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake" @ONLY)
    set(CPACK_GENERATOR NSIS)
    set(CPACK_NSIS_DISPLAY_NAME MusicPlayer)
    set(CPACK_PACKAGE_EXECUTABLES MusicPlayer MusicPlayer)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_INSTALL_BINDIR}")
    set(notice_dir "${CMAKE_INSTALL_DATADIR}/MusicPlayer/licenses")
elseif(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${qt_bin_dir}" NO_DEFAULT_PATH REQUIRED)
    install(TARGETS MusicPlayer BUNDLE DESTINATION .)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/DeployMac.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/DeployMac.cmake" @ONLY)
    set(CPACK_GENERATOR DragNDrop)
    set(notice_dir "MusicPlayer.app/Contents/Resources/licenses")
else()
    install(TARGETS MusicPlayer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.gyl30.MusicPlayer.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps" RENAME MusicPlayer.svg)
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_DEBIAN_PACKAGE_NAME musicplayer)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION sound)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # 插件通过 Qt 动态加载，不能由 ELF 的 NEEDED 自动推导。
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, libqt6sql6-sqlite, libqt6svg6")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, fonts-noto-cjk | fonts-wqy-zenhei")
    set(notice_dir "${CMAKE_INSTALL_DATADIR}/doc/musicplayer")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packaging/THIRD_PARTY_NOTICES.md" copyright_text)
    foreach(license IN ITEMS third/spdlog/LICENSE packaging/licenses/fmt-LICENSE.txt
            packaging/licenses/boost-LICENSE.txt)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${license}" license_text)
        string(APPEND copyright_text "\n\n--- ${license} ---\n${license_text}")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copyright" "${copyright_text}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/copyright" DESTINATION "${notice_dir}")
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/THIRD_PARTY_NOTICES.md" DESTINATION "${notice_dir}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third/spdlog/LICENSE" DESTINATION "${notice_dir}" RENAME spdlog-LICENSE.txt)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/licenses/fmt-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/licenses/boost-LICENSE.txt"
    DESTINATION "${notice_dir}"
)
if(WIN32)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake")
elseif(APPLE)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/DeployMac.cmake")
endif()

include(CPack)
