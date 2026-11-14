cmake_minimum_required(VERSION 3.16)
project(lanroom LANGUAGES C CXX)
if(APPLE)
	enable_language(OBJCXX)
endif()

# Ninja 配置会写出 compile_commands.json。Visual Studio 编译时改刷 build-clangd-win 或 build-clangd-linux。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json for clangd" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR})

include("${CMAKE_CURRENT_SOURCE_DIR}/openidea/framework.cmake")

include_directories(${CMAKE_SOURCE_DIR}/modules/include)

set(LAN_PROTO_CONFIG ${CMAKE_SOURCE_DIR}/protoconfig)
set(LAN_PROTO_FILES ${LAN_PROTO_CONFIG}/rpc.proto)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	${LAN_PROTO_FILES} ${LAN_PROTO_CONFIG}/module.proto)

execute_process(
	COMMAND ${PYTHON_BIN} ${PROTO_TOOL_DIR}/main.py
		${LAN_PROTO_CONFIG}/module.proto
		${CMAKE_SOURCE_DIR}/modules/include
		${CMAKE_SOURCE_DIR}/modules
		${CMAKE_SOURCE_DIR}/deploy
		-- ${LAN_PROTO_FILES}
	RESULT_VARIABLE LAN_PROTO_GEN
)
if(NOT LAN_PROTO_GEN EQUAL 0)
	message(FATAL_ERROR "protocol generation failed")
endif()

add_subdirectory(modules/lanserver)
add_subdirectory(modules/lanclient)

if(TARGET oi_clangd_db)
	add_dependencies(lanserver oi_clangd_db)
	add_dependencies(lanclient oi_clangd_db)
endif()
