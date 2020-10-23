#pragma once

namespace Jde::Markets::TwsWebSocket
{
	struct ProcessArg;
	struct Flex /*: IShutdown*/
	{
		static void SendTrades( const string& accountNumber, TimePoint start, TimePoint end, const ProcessArg& web  )noexcept;

	};
}