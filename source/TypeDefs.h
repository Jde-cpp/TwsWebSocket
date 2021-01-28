#pragma once
#include "../../MarketLibrary/source/Exports.h"
#pragma warning(disable:4100)
#pragma warning(disable:4127)
#pragma warning(disable:4244)
#pragma warning(disable:4996)
#pragma warning(disable:5054)
#include "../../MarketLibrary/source/types/proto/ib.pb.h"
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "../../MarketLibrary/source/types/proto/results.pb.h"
#pragma warning(default:4100)
#pragma warning(default:4127)
#pragma warning(default:4244)
#pragma warning(default:4996)
#pragma warning(default:5054)


namespace Jde::Markets::TwsWebSocket
{
	typedef uint32 SessionPK;
	typedef uint32 UserPK;
	typedef uint32 ClientPK;
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
/*		ClientKey( SessionPK SessionId ){}
		ClientKey( SessionPK sessionId, ClientPK clientId, UserPK userId )noexcept:SessionId{sessionId}, ClientId{clientId}, UserId{userId}{}
		ClientKey ForClientId()noexcept{ return ClientKey{} }:SessionId{sessionId}, ClientId{clientId}, UserId{userId}{}
		//size_t Handle()const noexcept{ return (size_t)SessionId << 32 | ClientId; }
		SessionPK SessionId{0};
		UserPK UserId{0};
	};*/
	typedef Proto::Results::MessageUnion MessageType; typedef sp<MessageType> MessageTypePtr;

	//extern shared_ptr<Settings::Container> SettingsPtr;

	using Proto::Requests::ERequests;
	using Proto::Results::EResults;
	using Proto::Results::ETickType;
	using boost::container::flat_set;
	using boost::container::flat_map;
}
