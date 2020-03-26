##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

cmake_minimum_required(VERSION 1.6.3)

PROJECT(mediastreamer)

#variable setting
SET(LIB_TYPE SHARED)
SET(MEDAISTREAMER_ROOT "lib")

IF(NOT DEFINED PROJECT_ROOT)
        GET_FILENAME_COMPONENT(PROJECT_ROOT "." ABSOLUTE)
ENDIF(NOT DEFINED PROJECT_ROOT)

#make the list with all the source files
FILE(GLOB SOURCES "*.c")

#prepare the include directories
INCLUDE_DIRECTORIES( "." )

find_package(PkgConfig REQUIRED)
pkg_check_modules(PKG_gstreamer REQUIRED gstreamer-1.0)
pkg_check_modules(PKG_gstapp REQUIRED gstreamer-app-1.0)

ADD_EXECUTABLE(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ${PKG_gstreamer_INCLUDE_DIRS} )

TARGET_LINK_LIBRARIES(${PROJECT_NAME}
                      mongoose
                      rdkloggers
                      ${PKG_gstreamer_LIBRARIES}
                      ${PKG_gstapp_LIBRARIES}        
                     )

INSTALL(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
