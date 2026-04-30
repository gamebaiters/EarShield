cmake_minimum_required(VERSION 3.16)
project(EarShield CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 COMPONENTS Core Widgets Gui Network REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/teamspeak
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(EARSHIELD_SOURCES
    src/plugin.cpp
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    src/updater_qt.cpp
    src/updater_qt.h
    src/debug_log.cpp
    src/debug_log.h
    src/version.h
    src/plugin.h
)

if(WIN32)
    set(EARSHIELD_TARGET EarShield_win64)
elseif(APPLE)
    set(EARSHIELD_TARGET earshield_mac)
else()
    set(EARSHIELD_TARGET earshield_linux_amd64)
endif()

add_library(${EARSHIELD_TARGET} SHARED ${EARSHIELD_SOURCES})

set_target_properties(${EARSHIELD_TARGET} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    PREFIX "lib"
)

if(WIN32)
    set_target_properties(${EARSHIELD_TARGET} PROPERTIES PREFIX "")
endif()

target_link_libraries(${EARSHIELD_TARGET} PRIVATE
    Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Network
)

if(APPLE)
    set_target_properties(${EARSHIELD_TARGET} PROPERTIES
        SUFFIX ".dylib"
    )
endif()
