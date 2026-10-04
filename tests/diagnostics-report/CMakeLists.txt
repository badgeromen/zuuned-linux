cmake_minimum_required(VERSION 3.21)
project(DiagnosticsReportTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Gui Concurrent Test QuickControls2 QuickDialogs2)
qt_standard_project_setup(REQUIRES 6.8)
set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ZuunedBuildInfo.h"
    "#pragma once\nnamespace ZuunedBuild { inline constexpr char version[] = \"diagnostics-test\"; inline constexpr char appRevision[] = \"app-fixture\"; inline constexpr char libzuneRevision[] = \"libzune-fixture\"; }\n")
qt_add_executable(diagnostics-report backend.cpp
    "${repo}/src/diagnostics/DiagnosticsService.cpp"
    "${repo}/src/diagnostics/SupportReport.cpp"
    "${repo}/src/diagnostics/SessionLog.cpp"
    "${repo}/src/diagnostics/Redactor.cpp")
target_include_directories(diagnostics-report PRIVATE "${repo}/src" "${repo}/src/diagnostics" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(diagnostics-report PRIVATE Qt6::Quick Qt6::Concurrent Qt6::Test)
foreach(component DiagnosticsSettings Theme)
    set_source_files_properties("${repo}/qml/${component}.qml" PROPERTIES QT_RESOURCE_ALIAS "${component}.qml")
endforeach()
set_source_files_properties("${repo}/qml/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(font PermanentMarker-Regular.ttf SpecimenAlien-Regular.ttf)
    set_source_files_properties("${repo}/qml/fonts/${font}" PROPERTIES QT_RESOURCE_ALIAS "fonts/${font}")
endforeach()
qt_add_qml_module(diagnostics-report URI Zuuned VERSION 1.0 NO_GENERATE_EXTRA_QMLDIRS
    SOURCES "${repo}/src/diagnostics/DiagnosticsService.h" "${repo}/src/AppSettings.h"
    QML_FILES "${repo}/qml/DiagnosticsSettings.qml" "${repo}/qml/Theme.qml"
    RESOURCES "${repo}/qml/fonts/PermanentMarker-Regular.ttf" "${repo}/qml/fonts/SpecimenAlien-Regular.ttf")
