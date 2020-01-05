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
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <nlohmann/json.hpp>
#include <EWrapper.h>
#include <EClientSocket.h>

#include "../../Framework/source/log/Logging.h"
#include "../../Framework/source/TypeDefs.h"
#include "../../Framework/source/JdeAssert.h"
#include "../../Framework/source/application/Application.h"
#include "../../Framework/source/DateTime.h"
#include "../../Framework/source/threading/Thread.h"
//#include "io/Buffer.h"

#include "../../Framework/source/io/File.h"
#include "../../Framework/source/Settings.h"
#include "../../Framework/source/threading/Interrupt.h"
#include "../../Framework/source/threading/InterruptibleThread.h"
#include "../../Framework/source/collections/Queue.h"
#include "../../Framework/source/collections/UnorderedMap.h"
#include "../../Framework/source/collections/UnorderedMapValue.h"
#include "../../Framework/source/StringUtilities.h"
#include "../../Framework/source/collections/UnorderedSet.h"

#include "../../MarketLibrary/source/TypeDefs.h"
#include "../../MarketLibrary/source/TwsProcessor.h"
//#include "markets/types/messages/IBEnums.h"

//#include <Eigen/Dense>
//#include <Eigen/Sparse>

#pragma warning(disable:4100)
#pragma warning(disable:4127)
#pragma warning(disable:4244)
#include "../../MarketLibrary/source/types/proto/ib.pb.h"
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "../../MarketLibrary/source/types/proto/results.pb.h"
#pragma warning(default:4100)
#pragma warning(default:4127)
#pragma warning(default:4244)
#include "TypeDefs.h"
