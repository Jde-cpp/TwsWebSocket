// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#ifdef _MSC_VER
	#define TWSAPIDLLEXP __declspec( dllimport )
	#include <SDKDDKVer.h>
#else
	#define TWSAPIDLLEXP
	#define IB_POSIX
#endif

#include <cassert>
#include <list>
#include <shared_mutex>

#ifndef __INTELLISENSE__
	#include <spdlog/spdlog.h>
	#include <spdlog/sinks/basic_file_sink.h>
	#include <spdlog/fmt/ostr.h>
#endif

#pragma warning( disable : 4245) 
#pragma warning( disable : 4701) 
#include <boost/crc.hpp> 
#pragma warning( default : 4245) 
#pragma warning( default : 4701) 
#include <boost/asio.hpp>
#include <boost/exception/diagnostic_information.hpp> 
//#include <boost/asio/ts/io_context.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <nlohmann/json.hpp>
#include <EWrapper.h>
#include <EClientSocket.h>

#include "log/Logging.h"
#include "../../Framework/source/TypeDefs.h"
#include "JdeAssert.h"
#include "application/Application.h"
#include "DateTime.h"
#include "threading/Thread.h"
//#include "io/Buffer.h"

#include "io/File.h"
#include "Settings.h"
#include "threading/Interrupt.h"
#include "threading/InterruptibleThread.h"
#include "collections/Queue.h"
#include "collections/UnorderedMap.h"
#include "collections/UnorderedMapValue.h"
#include "StringUtilities.h"
#include "collections/UnorderedSet.h"

#include "../../MarketLibrary/source/TypeDefs.h"
#include "TwsProcessor.h"
//#include "markets/types/messages/IBEnums.h"

//#include <Eigen/Dense>
//#include <Eigen/Sparse>


#pragma warning(disable:4127)
#pragma warning(disable:4244)
#include "types/proto/ib.pb.h"
#include "types/proto/requests.pb.h"
#include "types/proto/results.pb.h"
#pragma warning(default:4127)
#pragma warning(default:4244)
#include "TypeDefs.h"

namespace Jde::Markets::TwsWebSocket
{
	extern shared_ptr<Settings::Container> SettingsPtr;
}