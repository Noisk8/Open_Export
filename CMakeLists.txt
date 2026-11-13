cmake_minimum_required(VERSION 3.22)

project(open-deck-export
    VERSION 0.1.0
    DESCRIPTION "Open-source rekordbox-style USB export for Pioneer DJ players"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# ---- Dependencies -----------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    QuickLayouts
    Sql
)
find_package(TagLib REQUIRED)
find_package(SQLite3 REQUIRED)
# libcrypto (OpenSSL) powers the SQLCipher 4 writer for Device Library Plus.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# ---- Core library (no GUI, unit-testable) ----------------------------------
add_library(ode_core STATIC
    src/library/DatabaseManager.cpp
    src/library/TrackRepository.cpp
    src/library/PlaylistRepository.cpp
    src/library/AudioImportService.cpp
    src/library/ArtworkExtractor.cpp
    src/library/LibraryRelocator.cpp
    src/audio/AudioAnalysisService.cpp
    src/devices/UsbDevice.cpp
    src/devices/UsbDeviceManager.cpp
    src/export/devicesql/DeviceSqlString.cpp
    src/export/devicesql/DeviceSqlRow.cpp
    src/export/devicesql/DeviceSqlPage.cpp
    src/export/devicesql/DeviceSqlDatabase.cpp
    src/export/devicesql/DeviceSqlWriter.cpp
    src/export/devicesql/DeviceSqlReader.cpp
    src/export/anlz/AnlzWriter.cpp
    src/export/onelibrary/SqlCipherWriter.cpp
    src/export/onelibrary/DeviceLibraryWriter.cpp
    src/export/ExportManifest.cpp
    src/export/ExportManager.cpp
    src/export/ExportValidator.cpp
    src/ui/LibraryController.cpp
    src/ui/PlaylistController.cpp
    src/ui/DeviceController.cpp
    src/ui/CueController.cpp
    src/ui/AnalysisController.cpp
    src/ui/AnalysisWorker.cpp
    src/ui/ExportController.cpp
    src/ui/ExportWorker.cpp
    src/ui/ConversionController.cpp
    src/ui/ConversionWorker.cpp
)
target_include_directories(ode_core PUBLIC src)
target_link_libraries(ode_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    TagLib::tag
    SQLite::SQLite3
    OpenSSL::Crypto
)

# ---- Application ------------------------------------------------------------
qt_add_executable(open-deck-export
    src/main.cpp
)
target_link_libraries(open-deck-export PRIVATE
    ode_core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickLayouts
)

qt_add_qml_module(open-deck-export
    URI OpenDeckExport
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/OdeButton.qml
        src/ui/qml/OdeCheckBox.qml
        src/ui/qml/ConverterWindow.qml
)

# ---- Tests ------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(open-deck-tests
        tests/unit/test_database.cpp
    )
    target_link_libraries(open-deck-tests PRIVATE ode_core Qt6::Test)
    add_test(NAME unit COMMAND open-deck-tests)

    qt_add_executable(open-deck-tests-devicesql
        tests/unit/test_devicesql.cpp
    )
    target_link_libraries(open-deck-tests-devicesql PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-devicesql COMMAND open-deck-tests-devicesql)

    qt_add_executable(open-deck-tests-anlz
        tests/unit/test_anlz.cpp
    )
    target_link_libraries(open-deck-tests-anlz PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-anlz COMMAND open-deck-tests-anlz)

    qt_add_executable(open-deck-tests-ui
        tests/unit/test_ui_controllers.cpp
    )
    target_link_libraries(open-deck-tests-ui PRIVATE ode_core Qt6::Test Qt6::Qml Qt6::Quick)
    add_test(NAME unit-ui COMMAND open-deck-tests-ui)

    qt_add_executable(open-deck-tests-analysis
        tests/unit/test_analysis.cpp
    )
    target_link_libraries(open-deck-tests-analysis PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-analysis COMMAND open-deck-tests-analysis)

    qt_add_executable(open-deck-tests-devices
        tests/unit/test_devices.cpp
    )
    target_link_libraries(open-deck-tests-devices PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-devices COMMAND open-deck-tests-devices)

    qt_add_executable(open-deck-tests-export-controller
        tests/unit/test_export_controller.cpp
    )
    target_link_libraries(open-deck-tests-export-controller PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-export-controller COMMAND open-deck-tests-export-controller)

    qt_add_executable(open-deck-tests-integration
        tests/integration/test_import.cpp
    )
    target_link_libraries(open-deck-tests-integration PRIVATE ode_core Qt6::Test)
    add_test(NAME integration COMMAND open-deck-tests-integration)

    qt_add_executable(open-deck-tests-export
        tests/integration/test_export.cpp
    )
    target_link_libraries(open-deck-tests-export PRIVATE ode_core Qt6::Test)
    add_test(NAME integration-export COMMAND open-deck-tests-export)

    qt_add_executable(open-deck-tests-cues
        tests/unit/test_cues.cpp
    )
    target_link_libraries(open-deck-tests-cues PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-cues COMMAND open-deck-tests-cues)

    qt_add_executable(open-deck-tests-onelibrary
        tests/unit/test_onelibrary.cpp
    )
    target_link_libraries(open-deck-tests-onelibrary PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-onelibrary COMMAND open-deck-tests-onelibrary)

    qt_add_executable(open-deck-tests-incremental
        tests/integration/test_incremental.cpp
    )
    target_link_libraries(open-deck-tests-incremental PRIVATE ode_core Qt6::Test)
    add_test(NAME integration-incremental COMMAND open-deck-tests-incremental)

    qt_add_executable(open-deck-tests-conversion
        tests/unit/test_conversion.cpp
    )
    target_link_libraries(open-deck-tests-conversion PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-conversion COMMAND open-deck-tests-conversion)

    qt_add_executable(open-deck-tests-analysis-flow
        tests/unit/test_analysis_flow.cpp
    )
    target_link_libraries(open-deck-tests-analysis-flow PRIVATE ode_core Qt6::Test)
    add_test(NAME unit-analysis-flow COMMAND open-deck-tests-analysis-flow)

    qt_add_executable(open-deck-tests-missing-sources
        tests/integration/test_missing_sources.cpp
    )
    target_link_libraries(open-deck-tests-missing-sources PRIVATE ode_core Qt6::Test)
    add_test(NAME integration-missing-sources COMMAND open-deck-tests-missing-sources)
endif()

# ---- Packaging (Fase 7) -----------------------------------------------------
include(cmake/Packaging.cmake)
