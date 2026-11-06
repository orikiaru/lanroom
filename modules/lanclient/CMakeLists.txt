cmake_minimum_required(VERSION 3.16)

set(MODULE_SRCS
	src/main.cpp
	src/roomclient.cpp
	${CMAKE_SOURCE_DIR}/src/packet.cpp
	${CMAKE_SOURCE_DIR}/src/peer_channel.cpp
)
add_executable(lanclient ${MODULE_SRCS})
set_target_properties(lanclient PROPERTIES OUTPUT_NAME lanroom-client)
target_include_directories(lanclient PRIVATE
	inc
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/modules/include
)
target_link_libraries(lanclient PRIVATE proto_m_static net_m_static logger_m_static timer_m_static base_m_static jsoncpp_static)
if(WIN32)
	target_link_libraries(lanclient PRIVATE ws2_32)
endif()
