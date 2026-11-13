# ---------------------------------------------------------------------------
# Packaging and install rules for open-deck-export.
#
# Include this fragment from the top-level CMakeLists.txt AFTER the
# qt_add_executable(open-deck-export ...) call, for example:
#
#     include(cmake/Packaging.cmake)
#
# It is a plain include: it deliberately contains no project() and no
# cmake_minimum_required() call. It targets CMake >= 3.22, like the project.
# ---------------------------------------------------------------------------

include(GNUInstallDirs)

# ---- Install rules ----------------------------------------------------------
install(TARGETS open-deck-export
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    ${CMAKE_SOURCE_DIR}/resources/open-deck-export.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES
    ${CMAKE_SOURCE_DIR}/resources/open-deck-export.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

install(FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# Trailing slash installs the *contents* of docs/ into <docdir>/docs/.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
)

# ---- CPack metadata ---------------------------------------------------------
set(CPACK_PACKAGE_NAME "open-deck-export")
set(CPACK_PACKAGE_VENDOR "Open Deck Export")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
# Placeholder maintainer: adjust it to a real address before publishing.
set(CPACK_PACKAGE_CONTACT
    "Open Deck Export <open-deck-export@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Noisk8/Open_Export")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# ---- Debian / Ubuntu package ------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER
    "Open Deck Export <open-deck-export@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

# Runtime dependencies. NOTE: these are generic Debian/Ubuntu package names and
# may vary between distributions and releases (e.g. libssl3 vs libssl3t64,
# libtag1v5 vs libtag1v5-vanilla, or different Qt 6 package splits). Review and
# adjust them for the target distro before building a .deb.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6, libqt6gui6, libqt6qml6, libqt6quick6, libqt6sql6, libqt6widgets6, libtag1v5, libsqlite3-0, libssl3t64, ffmpeg, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs")

# ---- Generators -------------------------------------------------------------
# Respect a value supplied by the caller / the command line (e.g.
# -DCPACK_GENERATOR=DEB); only fall back to the default when it is undefined.
if(NOT DEFINED CPACK_GENERATOR)
    set(CPACK_GENERATOR "TGZ;DEB")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ")
# Regular expressions matched against source paths. They correspond to the
# globs /build/, /.git/, *~ (editor backups) and *.user (Qt Creator files).
# The extra "/build[^/]*/" variant also covers build dirs with a suffix, such
# as the default "build-release/" used by scripts/release.sh.
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/build[^/]*/"
    "/\\.git/"
    "~$"
    "\\.user$"
)

# CPack reads all the CPACK_* variables above at include time, so keep this
# as the very last statement of the fragment.
include(CPack)
