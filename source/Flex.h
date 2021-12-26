#pragma once

namespace Jde::Markets::TwsWebSocket
{
	struct ProcessArg;
	namespace Flex
	{
		α SendTrades( str accountNumber, TimePoint start, TimePoint end, ProcessArg web  )noexcept->Task;

	};
}