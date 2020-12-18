#pragma once
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "../../MarketLibrary/source/types/proto/results.pb.h"

namespace Jde::Markets::TwsWebSocket
{
	typedef uint32 SessionPK;
	typedef uint32 ClientPK;
	struct ClientKey
	{
		bool operator==(const ClientKey& x)const noexcept{ return x.SessionId==SessionId && x.ClientId==ClientId; }
		size_t Handle()const noexcept{ return (size_t)SessionId << 32 | ClientId; }
		SessionPK SessionId;
		ClientPK ClientId;
	};
	typedef Proto::Results::MessageUnion MessageType; typedef sp<MessageType> MessageTypePtr;

	extern shared_ptr<Settings::Container> SettingsPtr;

	using Proto::Requests::ERequests;
	using Proto::Results::EResults;
	using Proto::Results::ETickType;
	using boost::container::flat_set;
}
