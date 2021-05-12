#pragma once
#include <jde/markets/Exports.h>

DISABLE_WARNINGS
//#pragma warning(disable:4100)
//#pragma warning(disable:4127)
//#pragma warning(disable:4244)
//#pragma warning(disable:4996)
//#pragma warning(disable:5054)
#include <jde/markets/types/proto/ib.pb.h>
#include <jde/markets/types/proto/requests.pb.h>
#include <jde/markets/types/proto/results.pb.h>
ENABLE_WARNINGS
//#pragma warning(default:4100)
//#pragma warning(default:4127)
//#pragma warning(default:4244)
//#pragma warning(default:4996)
//#pragma warning(default:5054)


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
