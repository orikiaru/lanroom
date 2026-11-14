cmake_minimum_required(VERSION 3.16)

set(MODULE_SRCS
	src/main.cpp
	src/roomclient.cpp
	src/roomudp.cpp
	src/room_mgr.cpp
	src/nic_mgr.cpp
	src/nic_session.cpp
	src/tun_mgr.cpp
	${CMAKE_SOURCE_DIR}/modules/include/lan_addr.cpp
)
add_executable(lanclient ${MODULE_SRCS})
set_target_properties(lanclient PROPERTIES OUTPUT_NAME lanroom-client)
target_include_directories(lanclient PRIVATE
	inc
	${CMAKE_SOURCE_DIR}/modules/include
)
target_link_libraries(lanclient PRIVATE proto_m_static net_m_static logger_m_static base_m_static timer_m_static jsoncpp_static)
if(WIN32)
	target_sources(lanclient PRIVATE src/client_gui.cpp src/client_gui_session.cpp)
	target_include_directories(lanclient PRIVATE
		${CMAKE_SOURCE_DIR}/third_party/webview/core/include
		${CMAKE_SOURCE_DIR}/third_party/webview2/build/native/include
		${CMAKE_SOURCE_DIR}/third_party/wintun/include
	)
	target_compile_definitions(lanclient PRIVATE UNICODE _UNICODE)
	set_source_files_properties(src/client_gui.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")
	add_custom_command(TARGET lanclient POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_SOURCE_DIR}/third_party/webview2/build/native/x64/WebView2Loader.dll
			$<TARGET_FILE_DIR:lanclient>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_SOURCE_DIR}/third_party/wintun/bin/amd64/wintun.dll
			$<TARGET_FILE_DIR:lanclient>)
	target_link_libraries(lanclient PRIVATE ws2_32 iphlpapi)
elseif(APPLE)
	target_sources(lanclient PRIVATE src/client_gui_mac.mm src/client_gui_session.cpp)
	set_source_files_properties(src/client_gui_session.cpp PROPERTIES
		LANGUAGE OBJCXX
		COMPILE_OPTIONS "-Wno-deprecated-literal-operator;-Wno-auto-var-id")
	target_include_directories(lanclient PRIVATE
		${CMAKE_SOURCE_DIR}/third_party/webview/core/include
	)
	target_link_libraries(lanclient PRIVATE "-framework Cocoa" "-framework WebKit" "-framework CoreGraphics")
endif()
