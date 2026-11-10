cmake_minimum_required(VERSION 3.16)
project(lanroom LANGUAGES C CXX)

# Ninja 配置会写出 compile_commands.json。Visual Studio 编译时改刷 build-clangd-win 或 build-clangd-linux。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json for clangd" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR})

include("${CMAKE_CURRENT_SOURCE_DIR}/openidea/framework.cmake")

include_directories(${CMAKE_SOURCE_DIR}/modules/include)

execute_process(
	COMMAND ${PYTHON_BIN} ${PROTO_TOOL_DIR}/main.py
		${CMAKE_SOURCE_DIR}/xmlconfig/proto.xml
		${CMAKE_SOURCE_DIR}/xmlconfig/module.xml
		${CMAKE_SOURCE_DIR}/xmlconfig/module_component.xml
		${CMAKE_SOURCE_DIR}/modules/include
		${CMAKE_SOURCE_DIR}/modules
		${CMAKE_SOURCE_DIR}/deploy
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
