cmake_minimum_required(VERSION 3.16)
project(sigeon-apps VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(LIBXML2_REQUIRED REQUIRED IMPORTED_TARGET libxml-2.0)

set(APPS sigeon-hello sigeon-updater sigeon-drivers sigeon-davinci sigeon-store)

foreach(app ${APPS})
    add_executable(${app} ${app}.cpp)
    target_link_libraries(${app} PRIVATE PkgConfig::GTKMM Threads::Threads)
    if(${app} STREQUAL "sigeon-store")
        target_link_libraries(${app} PRIVATE PkgConfig::LIBXML2_REQUIRED)
    endif()
    install(TARGETS ${app} RUNTIME DESTINATION bin)
endforeach()
