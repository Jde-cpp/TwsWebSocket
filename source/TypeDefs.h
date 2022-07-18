#pragma once
#include <jde/markets/Exports.h>

DISABLE_WARNINGS
#include <jde/markets/types/proto/ib.pb.h>
#include <jde/markets/types/proto/requests.pb.h>
#include <jde/markets/types/proto/results.pb.h>
ENABLE_WARNINGS


namespace Jde::Markets::TwsWebSocket
{
	using SessionPK=uint32;
	using UserPK=uint32;
	using ClientPK=uint32;
	struct SessionKey
	{
		SessionPK SessionId{0};
		UserPK UserId{0};
	};

	struct ClientKey : SessionKey
	{
		bool operator==(const ClientKey& x)const noexcept{ return x.SessionId==SessionId && x.ClientId==ClientId; }
		ClientPK ClientId{0};
	};

	using MessageType=Proto::Results::MessageUnion;
	using MessageTypePtr=sp<MessageType>;


	using Proto::Requests::ERequests;
	using Proto::Results::EResults;
	using Proto::Results::ETickType;
	using boost::container::flat_set;
	using boost::container::flat_map;
}