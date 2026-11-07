cmake_minimum_required(VERSION 3.16)

set(MODULE_SRCS
	src/main.cpp
	src/roomserver.cpp
	src/roomudp.cpp
	src/room_mgr.cpp
	src/room.cpp
	${CMAKE_SOURCE_DIR}/modules/include/lan_addr.cpp
)
add_executable(lanserver ${MODULE_SRCS})
set_target_properties(lanserver PROPERTIES OUTPUT_NAME lanroom-server)
target_include_directories(lanserver PRIVATE
	inc
	${CMAKE_SOURCE_DIR}/modules/include
)
# 转发只在一条 poll 上跑。要扩容就多起进程。客户端另说，要接界面。
target_compile_definitions(lanserver PRIVATE _SINGLE_THREAD_)
target_link_libraries(lanserver PRIVATE redis_static proto_static net_static logger_static timer_static base_static jsoncpp_static)
if(WIN32)
	target_link_libraries(lanserver PRIVATE ws2_32 iphlpapi)
endif()
