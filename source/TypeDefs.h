#pragma once
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "../../MarketLibrary/source/types/proto/results.pb.h"

namespace Jde::Markets::TwsWebSocket
{
	typedef uint32 SessionId;
	typedef uint32 ClientRequestId;
	struct ClientKey
	{
		bool operator==(const ClientKey& x)const noexcept{return x.SessionPK==SessionPK && x.ClientId==ClientId;}
		SessionId SessionPK;
		ClientRequestId ClientId;
	};
	typedef Proto::Results::MessageUnion MessageType; typedef sp<MessageType> MessageTypePtr;

	extern shared_ptr<Settings::Container> SettingsPtr;

	using Proto::Requests::ERequests;
	using Proto::Results::EResults;
	using Proto::Results::ETickType;
	using boost::container::flat_set;
}
