#pragma once
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "../../MarketLibrary/source/types/proto/results.pb.h"

namespace Jde::Markets::TwsWebSocket
{
	typedef uint32 SessionId;
	typedef uint32 ClientRequestId;
	typedef Proto::Results::MessageUnion MessageType; typedef sp<MessageType> MessageTypePtr;

	extern shared_ptr<Settings::Container> SettingsPtr;

	using Proto::Requests::ERequests;
	using Proto::Results::EResults;
}
