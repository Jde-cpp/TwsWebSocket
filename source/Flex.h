#pragma once

namespace Jde::Markets::TwsWebSocket
{
	struct Flex : Application::IShutdown
	{
		static void SendTrades( SessionId sessionId, ClientRequestId clientId, string accountNumber, TimePoint date )noexcept;
	private:
		
	};
}