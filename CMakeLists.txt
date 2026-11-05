cmake_minimum_required(VERSION 3.19)
project(monitor_sistema VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

add_library(monitor_core STATIC
    src/core/telemetry.cpp
    src/core/format.cpp
)
target_link_libraries(monitor_core PUBLIC Qt6::Core)
target_include_directories(monitor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(monitor_ui STATIC
    src/ui/dashboard.cpp
    src/ui/dashboard_paint.cpp
    src/ui/theme.h
)
target_link_libraries(monitor_ui PUBLIC monitor_core Qt6::Widgets)

add_executable(monitor-sistema src/main.cpp)
target_link_libraries(monitor-sistema PRIVATE monitor_ui)

option(MON_BUILD_TESTS "Costruisci i test" ON)
if(MON_BUILD_TESTS)
    find_package(Qt6 6.2 QUIET COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_executable(test_core tests/test_core.cpp)
        target_link_libraries(test_core PRIVATE monitor_core Qt6::Test)
        add_test(NAME core COMMAND test_core)

        add_executable(test_dashboard tests/test_dashboard.cpp)
        target_link_libraries(test_dashboard PRIVATE monitor_ui Qt6::Test)
        add_test(NAME dashboard COMMAND test_dashboard)

        set_tests_properties(core dashboard PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

install(TARGETS monitor-sistema RUNTIME DESTINATION bin)
install(FILES assets/monitor-sistema.svg
        DESTINATION share/icons/hicolor/scalable/apps)

# Il file .desktop deve avere un Exec assoluto: il PATH di plasmashell non
# contiene ~/.local/bin. Viene generato al momento dell'installazione, così
# segue il --prefix realmente usato.
configure_file(packaging/install-desktop.cmake.in
               ${CMAKE_BINARY_DIR}/install-desktop.cmake @ONLY)
install(SCRIPT ${CMAKE_BINARY_DIR}/install-desktop.cmake)
