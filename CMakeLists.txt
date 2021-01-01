cmake_minimum_required(VERSION 3.16)
project(networking)

add_definitions(-DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#
# compiler options
#

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DDEBUG -D_DEBUG")

add_subdirectory(extern)

#
# utils library
#

add_library(utils STATIC
	utils/include/utils/time.h
	utils/src/time.cpp
	utils/include/utils/circularbuffer.h
	utils/include/utils/serialize.h
	utils/src/serialize.cpp
)

target_include_directories (utils PRIVATE src include)

target_include_directories (utils PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils/include>
	$<INSTALL_INTERFACE:utils/include>
)

#
# network common library
#

add_library (netcommon STATIC
	netcommon/include/netcommon/socket.h
	netcommon/src/socket.cpp
	netcommon/include/netcommon/reliability.h
	netcommon/src/reliability.cpp
	netcommon/include/netcommon/log.h
	netcommon/src/log.cpp
	netcommon/include/netcommon/netsimulator.h
	netcommon/src/netsimulator.cpp
)

target_link_libraries(netcommon PUBLIC utils)

target_include_directories (netcommon PRIVATE src include)

target_include_directories (netcommon PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/netcommon/include>
	$<INSTALL_INTERFACE:netcommon/include>
)

#
# network server library
#

add_library(netserver STATIC
	netserver/include/netserver/netserver.h
	netserver/src/netserver.cpp
)

target_link_libraries(netserver PRIVATE netcommon)

target_include_directories (netserver PRIVATE src include)

target_include_directories (netserver PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/netserver/include>
	$<INSTALL_INTERFACE:netserver/include>
)

#
# network client library
#

add_library(netclient STATIC
	netclient/include/netclient/netclient.h
	netclient/src/netclient.cpp
)

target_link_libraries (netclient PRIVATE netcommon)

target_include_directories (netclient PRIVATE src include)

target_include_directories (netclient PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/netclient/include>
	$<INSTALL_INTERFACE:netclient/include>
)

#
# simulation common library
#

add_library(simcommon STATIC
	simcommon/include/simcommon/protocol.h
	simcommon/include/simcommon/types.h
	simcommon/src/types.cpp
	simcommon/include/simcommon/simulation.h
	simcommon/src/simulation.cpp
	simcommon/include/simcommon/script.h
	simcommon/src/script.cpp
)

target_include_directories (simcommon PRIVATE src include)

target_include_directories (simcommon PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simcommon/include>
	$<INSTALL_INTERFACE:simcommon/include>
)

#
# simulation server library
#
add_library(simserver STATIC
	simserver/include/simserver/simserver.h
	simserver/src/simserver.cpp
)

target_link_libraries (simserver PUBLIC simcommon PRIVATE utils)

target_include_directories (simserver PRIVATE src include)

target_include_directories (simserver PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simserver/include>
	$<INSTALL_INTERFACE:simserver/include>
)

#
# simulation client library
#
add_library(simclient STATIC
	simclient/include/simclient/simclient.h
	simclient/src/simclient.cpp
)

target_link_libraries (simclient PUBLIC simcommon PRIVATE utils)

target_include_directories (simclient PRIVATE src include)

target_include_directories (simclient PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simclient/include>
	$<INSTALL_INTERFACE:simclient/include>
)

#
# server executable
#

add_executable (sample_server
	samples/sample_server/src/main.cpp
)

target_link_libraries(sample_server  PRIVATE netserver)

#
# client executable
#

find_package(glfw3)
find_package(OpenGL)
find_package(glew CONFIG)

add_executable (sample_listenserver
	samples/sample_listenserver/src/main.mm
	samples/sample_listenserver/src/imgui_impl_opengl2.cpp
	samples/sample_listenserver/src/imgui_impl_opengl2.h
	samples/sample_listenserver/src/imgui_impl_osx.h
	samples/sample_listenserver/src/imgui_impl_osx.mm
)

target_link_libraries(sample_listenserver PRIVATE netclient netserver netcommon simclient simserver imgui "-framework Foundation" "-framework Cocoa" OpenGL::GL)
