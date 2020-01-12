project(networking)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#
# compiler options
#

set (CMAKE_C_FLAGS                "-Wall -m64 -std=c99 -fPIC -Wno-unused-function -Wno-expansion-to-defined")
set (CMAKE_C_FLAGS_DEBUG          "-g -DDEBUG")
set (CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set (CMAKE_C_FLAGS_RELEASE        "-O4 -DNDEBUG")
set (CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
set (CMAKE_DEBUG_POSTFIX          "d")

#
# common library
#

add_library (common STATIC
	common/include/common/socket.h
	common/src/socket.cpp
	common/include/common/time.h
	common/src/time.cpp
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
)

target_link_libraries(server PRIVATE common)

#
# client library
#

add_executable (client
	client/src/main.cpp
)

target_link_libraries(client PRIVATE common)