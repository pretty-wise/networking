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
# common library
#

add_library (common STATIC
	common/include/common/socket.h
	common/src/socket.cpp
	common/include/common/time.h
	common/src/time.cpp
	common/include/common/reliability.h
	common/src/reliability.cpp
	common/include/common/log.h
	common/src/log.cpp
)

target_include_directories (common PRIVATE src include)

target_include_directories (common PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/include>
	$<INSTALL_INTERFACE:common/include>
)

#
# server library
#

add_executable (server
	server/src/main.cpp
	server/src/server.h
	server/src/server.cpp
)

target_link_libraries(server PRIVATE common)

#
# client library
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
	client/src/client.h
	client/src/client.cpp
)

target_link_libraries(client PRIVATE common imgui "-framework Foundation" "-framework Cocoa" OpenGL::GL)
