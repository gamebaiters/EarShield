cmake_minimum_required(VERSION 3.16)
project(EarShield CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/teamspeak
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# macOS: TeamSpeak 3 Client statically links Qt without exporting symbols, so
# loading a second copy of Qt into the process triggers qFatal as soon as
# QWidget construction sees a NULL QApplication::instance() in our private
# Qt's static state. Build the macOS plugin Qt-free: drop UI dialogs +
# Qt-based auto-updater. Audio processing is identical across platforms.
if(APPLE)
    set(EARSHIELD_SOURCES
        src/plugin.cpp
        src/debug_log.cpp
        src/debug_log.h
        src/version.h
        src/plugin.h
    )
else()
    set(CMAKE_AUTOMOC ON)
    find_package(Qt5 5.12 COMPONENTS Core Widgets Gui Network REQUIRED)
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
endif()

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

if(APPLE)
    set_target_properties(${EARSHIELD_TARGET} PROPERTIES SUFFIX ".dylib")
else()
    target_link_libraries(${EARSHIELD_TARGET} PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Network
    )
endif()
