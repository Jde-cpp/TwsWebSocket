#pragma once

namespace Jde::Markets::TwsWebSocket
{
	typedef uint32 SessionId;
	typedef uint32 ClientRequestId;
	typedef Proto::Results::MessageUnion MessageType; typedef sp<MessageType> MessageTypePtr;
}
