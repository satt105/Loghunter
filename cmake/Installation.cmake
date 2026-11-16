# Installation, deployment and packages.

include(GNUInstallDirs)

set(documentsFolder ${CMAKE_INSTALL_DOCDIR})
if(WIN32)
    set(documentsFolder .)
endif()

install(TARGETS LogHunter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${documentsFolder} RENAME LICENSE.txt)
install(FILES ${PROJECT_SOURCE_DIR}/NOTICE DESTINATION ${documentsFolder} RENAME NOTICE.txt)

if(WIN32)
    set(deployByDefault ON)
else()
    set(deployByDefault OFF)
endif()
option(LOGHUNTER_DEPLOY_QT "Copy the Qt libraries on install" ${deployByDefault})

if(LOGHUNTER_DEPLOY_QT)
    set(qtLanguages en)
    foreach(file IN LISTS LOGHUNTER_TRANSLATIONS)
        get_filename_component(name ${file} NAME_WE)
        string(REPLACE "loghunter_" "" language ${name})
        list(APPEND qtLanguages ${language})
    endforeach()
    list(REMOVE_DUPLICATES qtLanguages)
    list(JOIN qtLanguages "," qtLanguages)
    qt_generate_deploy_app_script(
        TARGET LogHunter
        OUTPUT_SCRIPT deployScript
        NO_COMPILER_RUNTIME
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler
            --skip-plugin-types generic,networkinformation,tls --exclude-plugins qjpeg,qgif
            --translations ${qtLanguages})
    install(SCRIPT ${deployScript})
endif()

if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
else()
    configure_file(${PROJECT_SOURCE_DIR}/resources/linux/loghunter.desktop.in
                   ${PROJECT_BINARY_DIR}/generated/loghunter.desktop @ONLY)
    install(FILES ${PROJECT_BINARY_DIR}/generated/loghunter.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/resources/linux/loghunter.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    foreach(size 16 24 32 48 64 128 256)
        install(FILES ${PROJECT_SOURCE_DIR}/resources/images/logo-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME loghunter.png)
    endforeach()
endif()

# ── Packages (CPack) ─────────────────────────────────────────────────────────
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR ${LOGHUNTER_PUBLISHER})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT_NAME})
set(CPACK_PACKAGE_EXECUTABLES LogHunter LogHunter)
set(CPACK_OUTPUT_FILE_PREFIX ${PROJECT_SOURCE_DIR}/dist)
set(CPACK_PACKAGE_DIRECTORY ${PROJECT_BINARY_DIR}/cpack)
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_PROJECT_CONFIG_FILE ${PROJECT_SOURCE_DIR}/cmake/CPackOptions.cmake)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "LogHunter-${PROJECT_VERSION}-windows-x64")

    set(CPACK_INNOSETUP_ICON_FILE ${PROJECT_SOURCE_DIR}/resources/loghunter.ico)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
    set(CPACK_INNOSETUP_LANGUAGES french english)
    set(CPACK_INNOSETUP_ARCHITECTURE x64)
    set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
    set(CPACK_INNOSETUP_RUN_EXECUTABLES LogHunter)
    set(CPACK_CREATE_DESKTOP_LINKS LogHunter)
    set(CPACK_INNOSETUP_SETUP_AppId "{{7F3C2A91-5B64-4E0D-9A8E-2C1D6B4F8E53}")
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequired lowest)
    set(CPACK_INNOSETUP_SETUP_PrivilegesRequiredOverridesAllowed dialog)
    set(CPACK_INNOSETUP_SETUP_ChangesAssociations yes)
    set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\LogHunter.exe")
    set(CPACK_INNOSETUP_EXTRA_SCRIPTS ${PROJECT_SOURCE_DIR}/cmake/associations.iss)
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_FILE_NAME "LogHunter-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(CPack)
