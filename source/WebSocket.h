#pragma once
#include <boost/asio.hpp>
#include <boost/beast/websocket.hpp>
#include "WebRequestWorker.h"
#include "../../Framework/source/threading/Interrupt.h"

//https://www.boost.org/doc/libs/1_71_0/libs/beast/example/websocket/server/sync/websocket_server_sync.cpp

//------------------------------------------------------------------------------
namespace boost::asio::ip{ class tcp; }
namespace Jde::Markets::TwsWebSocket
{
	namespace Messages{ struct Message; struct Application; struct Strings; }
	class WebSocket : Threading::Interrupt, public IShutdown
	{
		typedef boost::beast::websocket::stream<boost::asio::ip::tcp::socket> Stream;
	public:
		Ω Create( uint16 port, sp<TwsClientSync> pClient/*, pWrapper*/ )noexcept->WebSocket&;
		Ω Instance()noexcept->WebSocket&;
		Ω InstancePtr()noexcept->WebSocket*{ return _pInstance.get(); }
		Ω AddRequestSession( ClientKey clientId )noexcept->TickerId;

		α DoSession( sp<Stream> pSession )noexcept->void;
 		α OnTimeout()noexcept->void override;
		α OnAwake()noexcept->void override{ OnTimeout(); }//unexpected

		α Shutdown()noexcept->void override;
		sp<WebSendGateway> WebSend()noexcept{ return _pWebSend; }

		α AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept->void;
		α AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept->void;
		α AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept->void;

	private:
		α EraseSession( SessionPK id )noexcept->void;
		α AddRequest( SessionPK id, long reqId )noexcept->void;
		α Accept()noexcept->void;
		WebSocket()=delete;
		WebSocket( const WebSocket& )=delete;
		WebSocket& operator=( const WebSocket& )=delete;
		WebSocket( uint16 port, sp<TwsClientSync> pClient )noexcept;

		std::atomic<SessionPK> _sessionId{0};
		Collections::UnorderedMap<SessionPK,Stream> _sessions;
		Collections::UnorderedMap<SessionPK,Jde::Queue<MessageType>> _outgoing;//TODO move into regular container.

		UnorderedMapValue<TickerId,uint32> _historicalCrcs; mutable std::mutex _historicalCacheMutex;
		uint16 _port;
		sp<Threading::InterruptibleThread> _pAcceptor;
		sp<boost::asio::ip::tcp::acceptor> _pAcceptObject;

		static sp<WebSocket> _pInstance;
		sp<TwsClientSync> _pClientSync;
		sp<WebSendGateway> _pWebSend;
		WebRequestWorker _requestWorker;
	};
}
