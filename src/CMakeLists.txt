# ── Core logic (no graphical interface) ──────────────────────────────────────
add_library(loghunter_core STATIC
    core/evtx/Bytes.h
    core/evtx/EvtxFile.h core/evtx/EvtxFile.cpp
    core/evtx/BinXml.h core/evtx/BinXml.cpp
    core/evtx/Extraction.h core/evtx/Extraction.cpp
    core/logs/Content.h core/logs/Content.cpp
    core/logs/XmlEvent.h core/logs/XmlEvent.cpp
    core/logs/Log.h core/logs/Log.cpp
    core/logs/TabularLog.h core/logs/TabularLog.cpp
    core/logs/XmlLog.h core/logs/XmlLog.cpp
    core/Calendar.h
    core/Format.h core/Format.cpp
    core/Actions.h core/Actions.cpp
    core/DateTime.h core/DateTime.cpp
    core/Normalization.h core/Normalization.cpp
    core/AccessAnalyzer.h core/AccessAnalyzer.cpp
    core/Catalog.h core/Catalog.cpp
    core/Mitre.h core/Mitre.cpp
    core/sigma/Yaml.h core/sigma/Yaml.cpp
    core/sigma/Rule.h core/sigma/Rule.cpp
    core/sigma/Engine.h core/sigma/Engine.cpp
    core/Sqlite.h core/Sqlite.cpp
    core/Blocks.h core/Blocks.cpp
    core/Schema.h
    core/Session.h core/Session.cpp
    core/Filters.h core/Filters.cpp
    core/Scan.h core/Scan.cpp
    core/Queries.h core/Queries.cpp
    core/Alerts.h core/Alerts.cpp
    core/Import.h core/Import.cpp
    core/Export.h core/Export.cpp
)
target_include_directories(loghunter_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(loghunter_core
    PUBLIC Qt6::Core Qt6::Concurrent sqlite3
    PRIVATE miniz loghunter_options)
file(GLOB rules CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/rules/*.yml)
qt_add_resources(loghunter_core "loghunter_rules"
    PREFIX "/rules" BASE ${PROJECT_SOURCE_DIR}/resources/rules FILES ${rules})

# ── Rendering: charts and reports (no widgets) ───────────────────────────────
add_library(loghunter_render STATIC
    render/Charts.h render/Charts.cpp
    render/Report.h render/Report.cpp
)
target_link_libraries(loghunter_render
    PUBLIC loghunter_core Qt6::Gui Qt6::Svg
    PRIVATE loghunter_options)

# ── Graphical interface ──────────────────────────────────────────────────────
add_library(loghunter_ui STATIC
    ui/Application.h ui/Application.cpp
    ui/Workspace.h ui/Workspace.cpp
    ui/Executor.h ui/Executor.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/Icons.h ui/Icons.cpp
    ui/Settings.h ui/Settings.cpp
    ui/Theme.h ui/Theme.cpp
    ui/dialogs/MessageDialog.h ui/dialogs/MessageDialog.cpp
    ui/dialogs/AboutDialog.h ui/dialogs/AboutDialog.cpp
    ui/dialogs/GuideDialog.h ui/dialogs/GuideDialog.cpp
    ui/dialogs/ImportDialog.h ui/dialogs/ImportDialog.cpp
    ui/dialogs/SettingsDialog.h ui/dialogs/SettingsDialog.cpp
    ui/dialogs/RulesDialog.h ui/dialogs/RulesDialog.cpp
    ui/dialogs/ReportDialog.h ui/dialogs/ReportDialog.cpp
    ui/dialogs/SourcesDialog.h ui/dialogs/SourcesDialog.cpp
    ui/pages/Page.h ui/pages/Page.cpp
    ui/pages/HomePage.h ui/pages/HomePage.cpp
    ui/pages/AlertsPage.h ui/pages/AlertsPage.cpp
    ui/pages/EventsPage.h ui/pages/EventsPage.cpp
    ui/pages/InvestigationPage.h ui/pages/InvestigationPage.cpp
    ui/pages/DashboardPage.h ui/pages/DashboardPage.cpp
    ui/widgets/FilterBar.h ui/widgets/FilterBar.cpp
    ui/widgets/Sidebar.h ui/widgets/Sidebar.cpp
    ui/widgets/Timeline.h ui/widgets/Timeline.cpp
    ui/widgets/Components.h ui/widgets/Components.cpp
    ui/widgets/EventModel.h ui/widgets/EventModel.cpp
    ui/widgets/TableModel.h ui/widgets/TableModel.cpp
    ui/widgets/DetailPanel.h ui/widgets/DetailPanel.cpp
    ui/widgets/FilterPanel.h ui/widgets/FilterPanel.cpp
    ui/widgets/EventTable.h ui/widgets/EventTable.cpp
    ui/widgets/ProfileView.h ui/widgets/ProfileView.cpp
    ui/widgets/ChartViews.h ui/widgets/ChartViews.cpp
)
target_link_libraries(loghunter_ui
    PUBLIC loghunter_render Qt6::Widgets
    PRIVATE loghunter_options)
if(WIN32)
    target_link_libraries(loghunter_ui PRIVATE dbghelp)
endif()

set(images)
foreach(size 16 24 32 48 64 128 256)
    list(APPEND images ${PROJECT_SOURCE_DIR}/resources/images/logo-${size}.png)
endforeach()
qt_add_resources(loghunter_ui "loghunter_images"
    PREFIX "/images" BASE ${PROJECT_SOURCE_DIR}/resources/images FILES ${images})
qt_add_resources(loghunter_ui "loghunter_documents"
    PREFIX "/documents" BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE)

# ── Executable ───────────────────────────────────────────────────────────────
qt_add_executable(LogHunter WIN32 app/main.cpp)
if(WIN32)
    configure_file(${PROJECT_SOURCE_DIR}/cmake/loghunter.rc.in ${PROJECT_BINARY_DIR}/generated/loghunter.rc @ONLY)
    target_sources(LogHunter PRIVATE ${PROJECT_BINARY_DIR}/generated/loghunter.rc)
    set_source_files_properties(${PROJECT_BINARY_DIR}/generated/loghunter.rc
        PROPERTIES OBJECT_DEPENDS ${PROJECT_SOURCE_DIR}/resources/loghunter.ico)
endif()
target_link_libraries(LogHunter PRIVATE loghunter_ui loghunter_options)

# ── Languages: every .ts file is compiled into the executable ────────────────
qt_add_translations(TARGETS LogHunter
    SOURCE_TARGETS loghunter_core loghunter_render loghunter_ui LogHunter
    TS_FILES ${LOGHUNTER_TRANSLATIONS}
    RESOURCE_PREFIX /translations
    LUPDATE_OPTIONS -locations none -no-obsolete)
