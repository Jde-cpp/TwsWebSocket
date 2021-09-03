#export CXX=g++-8
cmake_minimum_required(VERSION 3.16)
project(Jde.TwsWebSocket)
set( CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../Framework/cmake )
include(SetUpConfigurations)
link_directories( ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
#Custom######################################################
include_directories( "$ENV{REPO_DIR}/tws-api/source/cppclient/client" )
include_directories( "${PROJECT_SOURCE_DIR}/../../Public" )
#add_definitions( -DJDE_EDGAR )
#######################################################
add_executable( Jde.TwsWebSocket main.cpp BlocklyWorker.cpp Flex.cpp PreviousDayValues.cpp TwsSendWorker.cpp WebRequestWorker.cpp WebSendGateway.cpp WebSocket.cpp WebCoSocket.cpp WrapperWeb.cpp WatchListData.cpp
	requests/EdgarRequests.cpp requests/News.cpp requests/Twitter.cpp requests/Reddit.cpp )
#######################################################
#target_link_libraries(Jde.TwsWebSocket Threads::Threads)
target_link_libraries(Jde.TwsWebSocket Jde)
target_link_libraries(Jde.TwsWebSocket Jde.Blockly)
target_link_libraries(Jde.TwsWebSocket Jde.Markets)
target_link_libraries(Jde.TwsWebSocket Jde.XZ)
target_link_libraries(Jde.TwsWebSocket Jde.Ssl)
target_link_libraries(Jde.TwsWebSocket Jde.Markets.Edgar)
target_link_libraries(Jde.TwsWebSocket Ib)
target_link_libraries(Jde.TwsWebSocket lzma)
target_link_libraries(Jde.TwsWebSocket protobuf)
target_link_libraries(Jde.TwsWebSocket crypto)
target_link_libraries(Jde.TwsWebSocket ssl)
target_link_libraries(Jde.TwsWebSocket boost_context )
target_link_libraries(Jde.TwsWebSocket boost_thread )
target_link_libraries(Jde.TwsWebSocket boost_chrono )
target_link_libraries(Jde.TwsWebSocket boost_coroutine)
target_link_libraries(Jde.TwsWebSocket boost_iostreams)
target_link_libraries(Jde.TwsWebSocket stdc++)
#target_link_libraries(Jde.TwsWebSocket stdc++fs)


target_link_libraries(Jde.TwsWebSocket dl)
#######################################################
target_precompile_headers(Jde.TwsWebSocket
	PRIVATE
		<cassert>
		<list>
		<shared_mutex>
		<boost/crc.hpp>
		<boost/container/flat_set.hpp>
		<boost/core/noncopyable.hpp>
		<boost/exception/diagnostic_information.hpp>
		<EClientSocket.h>
		<EWrapper.h>
		<Execution.h>
		<Order.h>

		<jde/TypeDefs.h>
		<jde/coroutine/Task.h>
		 <jde/markets/TypeDefs.h>
		"../../Framework/source/collections/Queue.h"
		"../../Ssl/source/Ssl.h"
		"./TypeDefs.h"
		"WebRequestWorker.h"
)
