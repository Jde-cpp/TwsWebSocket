#pragma once
#include "WebRequestWorker.h"


//https://www.boost.org/doc/libs/1_71_0/libs/beast/example/websocket/server/sync/websocket_server_sync.cpp

//------------------------------------------------------------------------------
namespace boost::asio::ip{ class tcp; }
namespace Jde::Markets
{
	//struct TwsClient;

namespace TwsWebSocket
{
	//enum class EWebReceive : short;
	namespace Messages{ struct Message; struct Application; struct Strings; }
	class WebSocket : Threading::Interrupt, public IShutdown
	{
		typedef boost::beast::websocket::stream<boost::asio::ip::tcp::socket> Stream;
	public:
		static WebSocket& Create( uint16 port, sp<TwsClientSync> pClient/*, pWrapper*/ )noexcept;
		static WebSocket& Instance()noexcept;
		static WebSocket* InstancePtr()noexcept{ return _pInstance.get(); }
		void DoSession( sp<Stream> pSession )noexcept;
		static TickerId AddRequestSession( ClientKey clientId )noexcept;

 		void OnTimeout()noexcept override;
		void OnAwake()noexcept override{ OnTimeout(); }//unexpected

		//void ContractDetailsEnd( ReqId reqId );

		void Shutdown()noexcept override;
		sp<WebSendGateway> WebSend()noexcept{ return _pWebSend; }

		void AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept;
		void AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept;
		void AddOutgoing( const vector<const Proto::Results::MessageUnion>& messages, SessionPK id )noexcept;

	private:
		void EraseSession( SessionPK id )noexcept;
		void AddRequest( SessionPK id, long reqId )noexcept;
		void PushAccountRequest( const string& accountNumber, function<void(MessageType&)> setMessage )noexcept;

		std::atomic<SessionPK> _sessionId{0};
		Collections::UnorderedMap<SessionPK,Stream> _sessions;

		void Accept()noexcept;
		WebSocket()=delete;
		WebSocket( const WebSocket& )=delete;
		WebSocket& operator=( const WebSocket& )=delete;
		WebSocket( uint16 port, sp<TwsClientSync> pClient )noexcept;

		Collections::UnorderedMap<SessionPK,Jde::Queue<MessageType>> _outgoing;//TODO move into regular container.


		UnorderedMapValue<TickerId,uint32> _historicalCrcs; mutable std::mutex _historicalCacheMutex;
		uint16 _port;
		shared_ptr<Threading::InterruptibleThread> _pAcceptor;
		shared_ptr<boost::asio::ip::tcp::acceptor> _pAcceptObject;

		static shared_ptr<WebSocket> _pInstance;
		sp<TwsClientSync> _pClientSync;
		sp<WebSendGateway> _pWebSend;
		WebRequestWorker _requestWorker;
	};
}}
