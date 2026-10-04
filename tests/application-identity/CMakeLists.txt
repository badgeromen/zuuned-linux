cmake_minimum_required(VERSION 3.21)
project(ApplicationIdentityTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui)
qt_standard_project_setup(REQUIRES 6.8)

set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
qt_add_executable(application-identity
    backend.cpp
    "${repo}/src/ApplicationIdentity.cpp"
    "${repo}/src/ApplicationIdentity.h")
target_include_directories(application-identity PRIVATE "${repo}/src")
target_link_libraries(application-identity PRIVATE Qt6::Gui)
qt_add_resources(application-identity "zuuned_app_identity"
    PREFIX "/zuuned/app"
    BASE "${repo}/packaging/icons/hicolor/256x256/apps"
    FILES "${repo}/packaging/icons/hicolor/256x256/apps/zuuned.png")
