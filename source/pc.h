// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//
#include <jde/TypeDefs.h>
#pragma warning( push )
#ifdef _MSC_VER
	#define TWSAPIDLLEXP __declspec( dllimport )
	#include <SDKDDKVer.h>
	#pragma push_macro("assert")
	#undef assert
	#include <platformspecific.h>
	#pragma pop_macro("assert")
#else
	#define TWSAPIDLLEXP
	#define IB_POSIX
#endif
#include <cassert>
#include <list>
#include <shared_mutex>

#pragma warning( disable : 4245 )
#pragma warning( disable : 4267 )
#pragma warning( disable : 4505 )
#pragma warning( disable : 4701 )
#include <boost/crc.hpp>

#include <boost/container/flat_set.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <EClientSocket.h>
#include <EWrapper.h>
#include <Execution.h>
#include <Order.h>
#pragma warning( pop )
#include "../../Framework/source/collections/Queue.h"
#include <jde/markets/TypeDefs.h>
#include "./TypeDefs.h"
#include "WebRequestWorker.h"
