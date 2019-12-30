#export CXX=g++-8
cmake_minimum_required(VERSION 3.16)
project(Jde.TwsWebSocket)
set( CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../Framework/cmake )
include(SetUpConfigurations)
link_directories( ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
#Custom######################################################
include_directories( "/home/duffyj/code/libraries/tws-api/source/cppclient/client" )
#######################################################
add_executable( Jde.TwsWebSocket main.cpp WebSocket.cpp WebSocketReceive.cpp WebSocketSend.cpp WrapperWeb.cpp Flex.cpp )
#######################################################
target_link_libraries(Jde.TwsWebSocket Threads::Threads)
target_link_libraries(Jde.TwsWebSocket Jde)
target_link_libraries(Jde.TwsWebSocket Jde.Markets)
target_link_libraries(Jde.TwsWebSocket Ib)
target_link_libraries(Jde.TwsWebSocket protobuf)
target_link_libraries(Jde.TwsWebSocket stdc++fs)
target_link_libraries(Jde.TwsWebSocket dl)
#######################################################
target_precompile_headers(Jde.TwsWebSocket
	PRIVATE
		<cassert>
		<list>
		<shared_mutex>
		<spdlog/spdlog.h>
		<spdlog/sinks/basic_file_sink.h>
		<spdlog/fmt/ostr.h>
		<boost/crc.hpp> 
		<boost/asio.hpp>
		<boost/exception/diagnostic_information.hpp> 
		<boost/beast/core.hpp>
		<boost/beast/websocket.hpp>
		<boost/core/noncopyable.hpp>
		<boost/property_tree/ptree.hpp>
		<boost/property_tree/xml_parser.hpp>
		<nlohmann/json.hpp>
		<EWrapper.h>
		<EClientSocket.h>
		"../../Framework/source/log/Logging.h"
		"../../Framework/source/TypeDefs.h"
		"../../Framework/source/JdeAssert.h"
		"../../Framework/source/application/Application.h"
		"../../Framework/source/DateTime.h"
		"../../Framework/source/threading/Thread.h"
		"../../Framework/source/io/File.h"
		"../../Framework/source/Settings.h"
		"../../Framework/source/threading/Interrupt.h"
		"../../Framework/source/threading/InterruptibleThread.h"
		"../../Framework/source/collections/Queue.h"
		"../../Framework/source/collections/UnorderedMap.h"
		"../../Framework/source/collections/UnorderedMapValue.h"
		"../../Framework/source/StringUtilities.h"
		"../../Framework/source/collections/UnorderedSet.h"
		"../../MarketLibrary/source/TypeDefs.h"
		"../../MarketLibrary/source/TwsProcessor.h"
		"../../MarketLibrary/source/types/proto/ib.pb.h"
		"../../MarketLibrary/source/types/proto/requests.pb.h"
		"../../MarketLibrary/source/types/proto/results.pb.h"
		"TypeDefs.h"
)
