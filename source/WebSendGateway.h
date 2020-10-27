#pragma once
//#include <span>
#include <boost/core/noncopyable.hpp>

namespace Jde::Markets::TwsWebSocket
{
	class WebSocket;
	struct WebSendGateway : enable_shared_from_this<WebSendGateway>, boost::noncopyable //TODO not a worker, WebSendGateway
	{
		typedef tuple<SessionId,MessageTypePtr> QueueType;
		WebSendGateway( WebSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
		void EraseSession( SessionId id )noexcept;
		void AddExecutionRequest( SessionId id ){ unique_lock l{_executionRequestMutex}; _executionRequests.emplace( id ); }
		void AddOrderSubscription( OrderId orderId, SessionId sessionId )noexcept;
		bool AddAccountSubscription( const string& account, SessionId sessionId )noexcept;
		tuple<TickerId,flat_set<Proto::Requests::ETickList>> AddMarketDataSubscription( ContractPK contractId, flat_set<Proto::Requests::ETickList>&& ticks, SessionId sessionId )noexcept;
		tuple<TickerId,flat_set<Proto::Requests::ETickList>> RemoveMarketDataSubscription( ContractPK contractId, SessionId sessionId, bool haveLock=false )noexcept;
		bool CancelAccountSubscription( const string& account, SessionId sessionId )noexcept;
		void AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key );

		TickerId AddRequestSession( const ClientKey& key )noexcept{ const auto ib = _pClientSync->RequestId(); _requestSession.emplace(ib, key); return ib; }
		TickerId AddRequestSession( const ClientKey& key, TickerId ib )noexcept{ auto value = ib; if( value ) _requestSession.emplace(ib, key); else value = AddRequestSession(key); return value; }
		TickerId RequestFind( const ClientKey& key )const noexcept;
		void RequestErase( TickerId id )noexcept{ _requestSession.erase(id); }

		bool HasHistoricalRequest( TickerId id )const noexcept{ return _historicalCrcs.Has(id); }

		void Push( const Exception& e, const ClientKey& key )noexcept{ PushError( -1, e.what(), key );}
		void Push( const IBException& e, const ClientKey& key )noexcept{ PushError( e.ErrorCode, e.what(), key );}
		void PushError( int errorCode, string_view errorString, TickerId id )noexcept;
		void PushError( int errorCode, const string& errorString, const ClientKey& key )noexcept;


		void Push( MessageTypePtr pUnion, SessionId id )noexcept;
		void Push( const vector<MessageTypePtr>& pUnion, SessionId id )noexcept;

		void Push( EResults eResults, const ClientKey& key )noexcept;
		void Push( EResults eResults, TickerId ibReqId )noexcept;
		void Push( EResults eResults, function<void(MessageType&)> set )noexcept;

		bool Push( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept;
		void PushMarketData( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept;

		void PushAccountDownloadEnd( const string& accountNumber )noexcept;
		void Push( const Proto::Results::PortfolioUpdate& pMessage )noexcept;
		//void PushAllocated( unique_ptr<Proto::Results::AccountUpdateMulti> pMessage )noexcept;
		void Push( const Proto::Results::AccountUpdate& accountUpdate )noexcept;
		void ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, TickerId tickerId )noexcept;
		void Push( const Proto::Results::CommissionReport& report )noexcept;
		void AccountRequest( const string& accountNumber, function<void(MessageType&)> setMessage )noexcept;
		void AddRequestSessions( SessionId id, const vector<Proto::Results::EResults>& webSendMessages )noexcept;

		void SetClientSync( sp<TwsClientSync> pClient )noexcept{ DBG0( "WebSendGateway::SetClientSync"sv ); _pClientSync = pClient; }
	private:
		void Push( string&& data, SessionId sessionId )noexcept;
		ClientKey GetClientRequest( TickerId ibReqId )noexcept{return _requestSession.Find( ibReqId, {0,0} ); }

		void Run()noexcept;
		void HandleRequest( SessionId sessionId, string&& data )noexcept;

		flat_map<string,boost::container::flat_set<SessionId>> _accountSubscriptions; std::shared_mutex _accountSubscriptionMutex;
		flat_map<ClientRequestId,flat_set<TickerId>> _multiRequests; mutable std::mutex _multiRequestMutex;//ie ask for multiple contractDetails
		flat_set<SessionId> _executionRequests; mutable std::shared_mutex _executionRequestMutex;

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		WebSocket& _webSocket;
		sp<TwsClientSync> _pClientSync;
		UnorderedMapValue<TickerId,uint32> _historicalCrcs; //mutable std::mutex _historicalCacheMutex;
		UnorderedMapValue<TickerId,ClientKey> _requestSession;//single session single call
		Collections::UnorderedMap<Proto::Results::EResults,UnorderedSet<SessionId>> _requestSessions;//multiple sessions can request item, ie market data.
		flat_map<OrderId,flat_set<SessionId>> _orderSubscriptions; mutable std::mutex _orderSubscriptionMutex;
		tuple<TickerId, flat_set<Proto::Requests::ETickList>> MarketDataTicks( ContractPK contractId )noexcept;

		flat_map<TickerId,ContractPK> _marketTicketContractMap;//todo move _market* to class
		flat_map<ContractPK,flat_map<SessionId,flat_set<Proto::Requests::ETickList>>> _marketSessionSubscriptions;
		flat_map<ContractPK,tuple<TickerId,flat_set<Proto::Requests::ETickList>>> _marketSubscriptions; std::mutex _marketSubscriptionsMutex;
	};
}