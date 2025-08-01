# Webcamoid, webcam capture application.
# Copyright (C) 2021  Gonzalo Exequiel Pedone
#
# Webcamoid is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Webcamoid is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
#
# Web-Site: http://webcamoid.github.io/

cmake_minimum_required(VERSION 3.16)

project(VirtualCamera_v4l2lb LANGUAGES CXX)

include(CheckCXXSourceCompiles)
include(../../../../cmake/ProjectCommons.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_COMPONENTS
    Core
    Gui)
find_package(QT NAMES Qt${QT_VERSION_MAJOR} COMPONENTS
             ${QT_COMPONENTS}
             REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} ${QT_MINIMUM_VERSION} COMPONENTS
             ${QT_COMPONENTS}
             REQUIRED)
find_package(PkgConfig)

check_cxx_source_compiles("
#ifndef __FreeBSD__
    #error Not BSD
#endif

int main()
{
    return 0;
}" IS_BSD)

if (IS_BSD)
    set(CMAKE_REQUIRED_INCLUDES /usr/local/include)
endif ()

check_cxx_source_compiles("
#include <linux/videodev2.h>

int main()
{
    return 0;
}" HAVE_V4L2)

pkg_check_modules(LIBKMOD libkmod)

if (LIBKMOD_FOUND)
    link_directories(${LIBKMOD_LIBRARY_DIRS})
endif ()

set(SOURCES
    ../vcam.cpp
    ../vcam.h
    src/plugin.cpp
    src/plugin.h
    src/vcamv4l2lb.cpp
    src/vcamv4l2lb.h
    pspec.json)

if (NOT NOV4L2 AND HAVE_V4L2 AND NOT ANDROID)
    qt_add_plugin(VirtualCamera_v4l2lb
                  SHARED
                  CLASS_NAME Plugin)
    target_sources(VirtualCamera_v4l2lb PRIVATE
                   ${SOURCES})
else ()
    add_library(VirtualCamera_v4l2lb EXCLUDE_FROM_ALL ${SOURCES})
endif ()

set_target_properties(VirtualCamera_v4l2lb PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILDDIR}/${AKPLUGINSDIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILDDIR}/${AKPLUGINSDIR})

if (IPO_IS_SUPPORTED)
    set_target_properties(VirtualCamera_v4l2lb PROPERTIES
                          INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

add_dependencies(VirtualCamera_v4l2lb avkys)
target_include_directories(VirtualCamera_v4l2lb
                           PUBLIC
                           ${LIBKMOD_INCLUDE_DIRS}
                           PRIVATE
                           ..
                           ../../../../Lib/src)
target_compile_definitions(VirtualCamera_v4l2lb PRIVATE AVKYS_PLUGIN_VIRTUALCAMERA_V4L2LB)
list(TRANSFORM QT_COMPONENTS PREPEND Qt${QT_VERSION_MAJOR}:: OUTPUT_VARIABLE QT_LIBS)
target_link_libraries(VirtualCamera_v4l2lb
                      ${QT_LIBS}
                      ${LIBKMOD_LIBRARIES}
                      avkys)

if (LIBKMOD_FOUND)
    add_definitions(-DHAVE_LIBKMOD)
endif ()

if (NOT "${EXTRA_SUDOER_TOOL_DIR}" STREQUAL "")
    add_definitions(-DEXTRA_SUDOER_TOOL_DIR="${EXTRA_SUDOER_TOOL_DIR}")
endif ()

if (NOT NOV4L2 AND HAVE_V4L2 AND NOT ANDROID)
    install(TARGETS VirtualCamera_v4l2lb
            LIBRARY DESTINATION ${AKPLUGINSDIR}
            RUNTIME DESTINATION ${AKPLUGINSDIR})
endif ()
