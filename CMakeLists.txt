cmake_minimum_required(VERSION 3.12)

project(Basic-Networking)

add_executable(${PROJECT_NAME}
	Dependencies/asio/include/asio.hpp
	Dependencies/asio/include/asio/ts/internet.hpp
	Dependencies/asio/include/asio/ts/buffer.hpp
	src/client.h
	src/connection.h
	src/message.h
	src/message_queue.h
	src/net_common.h
	src/peer.h
	src/server.h
	src/standalone_peer.h
	src/standalone_peer_gui.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC
	Dependencies/asio/include)

#Setting the default c++ version to c++17
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)