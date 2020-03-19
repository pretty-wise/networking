cmake_minimum_required(VERSION 3.16)
project(networking)

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
# network common library
#

add_library (netcommon STATIC
	netcommon/include/netcommon/socket.h
	netcommon/src/socket.cpp
	netcommon/include/netcommon/time.h
	netcommon/src/time.cpp
	netcommon/include/netcommon/reliability.h
	netcommon/src/reliability.cpp
	netcommon/include/netcommon/log.h
	netcommon/src/log.cpp
	netcommon/include/netcommon/circularbuffer.h
	netcommon/src/circularbuffer.cpp
	netcommon/include/netcommon/netsimulator.h
	netcommon/src/netsimulator.cpp
)

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
	simcommon/include/simcommon/time.h
	simcommon/src/time.cpp
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

target_link_libraries (simserver PUBLIC simcommon)

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

target_link_libraries (simclient PUBLIC simcommon)

target_include_directories (simclient PRIVATE src include)

target_include_directories (simclient PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simclient/include>
	$<INSTALL_INTERFACE:simclient/include>
)

#
# server executable
#

add_executable (server
	server/src/main.cpp
)

target_link_libraries(server PRIVATE netserver)

#
# client executable
#

find_package(glfw3)
find_package(OpenGL)
find_package(glew CONFIG)

add_executable (client
	client/src/main.mm
	client/src/imgui_impl_opengl2.cpp
	client/src/imgui_impl_opengl2.h
	client/src/imgui_impl_osx.h
	client/src/imgui_impl_osx.mm
)

target_link_libraries(client PRIVATE netclient netserver netcommon simclient simserver imgui "-framework Foundation" "-framework Cocoa" OpenGL::GL)
