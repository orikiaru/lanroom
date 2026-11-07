cmake_minimum_required(VERSION 3.16)
project(lanroom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ninja 配置后把 compile_commands.json 写到构建目录，供 clangd 跳转。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json for clangd" FORCE)

set(OI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/openidea)
set(BASE_LIB_PATH ${OI_ROOT}/cbaselib)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR})

if(MSVC)
	add_compile_options(/W3)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/std:c++20>)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
	add_compile_options("/FI${BASE_LIB_PATH}/port/win/msvc_prefix.h")
	add_compile_options(/external:W0)
else()
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wall>")
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wextra>")
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
	add_compile_options(-Wno-error)
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>")
endif()

include_directories(
	${BASE_LIB_PATH}/include
	${BASE_LIB_PATH}/base/inc
	${BASE_LIB_PATH}/net/inc
	${BASE_LIB_PATH}/timer/inc
	${BASE_LIB_PATH}/logger/inc
	${BASE_LIB_PATH}/redis/inc
	${BASE_LIB_PATH}/proto/inc
	${BASE_LIB_PATH}/port
)
include_directories(SYSTEM
	${BASE_LIB_PATH}/3rd/cpptoml
	${BASE_LIB_PATH}/3rd/libuv/include
	${BASE_LIB_PATH}/3rd/hiredis
	${BASE_LIB_PATH}/3rd/jsoncpp/include
)
if(MSVC)
	include_directories(${BASE_LIB_PATH}/port/win)
endif()

set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${BASE_LIB_PATH}/3rd/libuv EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/3rd/hiredis EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/3rd/jsoncpp EXCLUDE_FROM_ALL)
if(MSVC AND TARGET uv_a)
	target_compile_options(uv_a PRIVATE /W0)
endif()

add_subdirectory(${BASE_LIB_PATH}/base EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/net EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/proto EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/timer EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/logger EXCLUDE_FROM_ALL)
add_subdirectory(${BASE_LIB_PATH}/redis EXCLUDE_FROM_ALL)

include_directories(${CMAKE_SOURCE_DIR}/modules/include)

find_program(PYTHON_BIN NAMES python3 python)
if(NOT PYTHON_BIN)
	message(FATAL_ERROR "python is required to generate protocol")
endif()
execute_process(
	COMMAND ${PYTHON_BIN} ${OI_ROOT}/cbaselib/proto/proto_tool/main.py
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
