#pragma once
//#include "../framework/Application.h"
//https://www.boost.org/doc/libs/1_71_0/libs/beast/example/websocket/server/sync/websocket_server_sync.cpp

//------------------------------------------------------------------------------
namespace boost::asio::ip{ class tcp; }
namespace Jde::Markets
{
	struct TwsClient;

namespace TwsWebSocket
{
	//enum class EWebReceive : short;
	namespace Messages{ struct Message; struct Application; struct Strings; }
	class WebSocket : Threading::Interrupt, public IShutdown
	{
		typedef boost::beast::websocket::stream<boost::asio::ip::tcp::socket> Stream;
	public:
		static WebSocket& Create( uint16 port )noexcept;
		static WebSocket& Instance()noexcept;
		static WebSocket* InstancePtr()noexcept{ return _pInstance.get(); }
		void DoSession( sp<Stream> pSession )noexcept;

 		void OnTimeout()noexcept override;
		void OnAwake()noexcept override{ OnTimeout(); }//unexpected
		//void Push( EResults webSend, sp<google::protobuf::Message> pMessage );//{ _messages.Push( pMessage ); }

		tuple<SessionId,TickerId> GetClientRequest( ReqId ibReqId )noexcept{return _requestSession.Find( ibReqId, make_tuple(0,0) ); }
		void ContractDetailsEnd( ReqId reqId );
		void Push( Proto::Results::EResults webSend, sp<MessageType> pMessageUnion )noexcept;
		void Push( TickerId ibReqId, Proto::Results::EResults messageId )noexcept;
		void Push( const Proto::Results::Position& pPosition )noexcept;
		void Push( Proto::Results::PortfolioUpdate& pMessage )noexcept;
		void Push( Proto::Results::AccountUpdate& accountUpdate )noexcept;
		bool PushAllocated( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept;
		void PushAllocated( Proto::Results::AccountList* pPosition )noexcept;
		void PushAllocated( Proto::Results::AccountUpdateMulti* pMessage )noexcept;
		void PushAllocated( TickerId reqId, Proto::Results::HistoricalData* pMessage )noexcept;
		void PushAllocated( Proto::Results::StringResult* pMessage )noexcept;
		void PushAllocated( SessionId sessionId, Proto::Results::MessageValue* pMessage )noexcept;

		void PushAllocatedRequest( TickerId tickerId, Proto::Results::MessageValue* pMessage )noexcept;
		void PushAllocated( TickerId tickerId, Proto::Results::ContractDetails* pDetails )noexcept;
		void AddOutgoing( SessionId id, MessageTypePtr pAllocated )noexcept;
		void AddOutgoing( SessionId id, const vector<MessageTypePtr>& outgoing )noexcept;
		void AddError( TickerId id, int errorCode, const std::string& errorString )noexcept;
		void AddError( SessionId sessionId, ClientRequestId clientId, int errorCode, const std::string& errorString )noexcept;
		void AddError( SessionId sessionId, ClientRequestId clientId, const Exception& e )noexcept{ AddError( sessionId, clientId, -1, e.what() );}
		void Shutdown()noexcept override;
	private:
		void EraseSession( SessionId id )noexcept;
		void AddRequestSessions( SessionId id, const vector<Proto::Results::EResults>& webSendMessages )noexcept;
		void AddRequest( SessionId id, long reqId )noexcept;

		void ReceiveAccountUpdates( SessionId sessionId, const Proto::Requests::RequestAccountUpdates& request )noexcept;
		void ReceiveAccountUpdatesMulti( SessionId sessionId, const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates )noexcept;
		void ReceiveContractDetails( SessionId sessionId, const Proto::Requests::RequestContractDetails& request )noexcept;
		void ReceiveMarketDataSmart( SessionId sessionId, const Proto::Requests::RequestMrkDataSmart& request )noexcept;
		void ReceiveHistoricalData( SessionId sessionId, const Proto::Requests::RequestHistoricalData& options )noexcept;
		//void ReceiveOptions( SessionId sessionId, const Proto::Requests::RequestOptions& options )noexcept;
		void ReceiveRequests( SessionId sessionId, const Proto::Requests::GenericRequests& request )noexcept;
		void ReceiveFlex( SessionId sessionId, const Proto::Requests::FlexExecutions& req )noexcept;
		void ReceiveOrder( SessionId sessionId, ClientRequestId clientId, const Proto::Order& order, const Proto::Contract& contract )noexcept;
		TickerId FindRequestId( SessionId sessionId, ClientRequestId clientId )const noexcept;
		std::atomic<SessionId> _sessionId{0};
		Collections::UnorderedMap<SessionId,Stream> _sessions;

		void Accept()noexcept;
		WebSocket()=delete;
		WebSocket( const WebSocket& )=delete;
		WebSocket& operator=( const WebSocket& )=delete;
		WebSocket( uint16 port );

		Collections::UnorderedMap<SessionId,Jde::Queue<MessageType>> _outgoing;
		Collections::UnorderedMap<Proto::Results::EResults,Collections::UnorderedSet<SessionId>> _requestSessions;
		UnorderedMapValue<TickerId,tuple<SessionId,ClientRequestId>> _requestSession;
		unordered_map<ClientRequestId,unordered_set<TickerId>> _multiRequests; mutable std::mutex _multiRequestMutex;
		Collections::UnorderedSet<Proto::Requests::ERequests> _requests;
		unordered_map<string,unordered_set<SessionId>> _accountRequests; mutable std::shared_mutex _accountRequestMutex;

		uint16 _port;
		shared_ptr<Threading::InterruptibleThread> _pAcceptor;
		shared_ptr<boost::asio::ip::tcp::acceptor> _pAcceptObject;
		static shared_ptr<WebSocket> _pInstance;
	};
}}
