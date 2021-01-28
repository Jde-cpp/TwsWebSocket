#pragma once
#include <boost/container/flat_map.hpp>
#include <boost/core/noncopyable.hpp>
#include "../../MarketLibrary/source/client/TwsClientSync.h"

//namespace Jde::Markets{ struct TwsClientSync;}
namespace Jde::Markets::TwsWebSocket
{
	class WebSocket;
	struct WebSendGateway : std::enable_shared_from_this<WebSendGateway>, boost::noncopyable //TODO not a worker, WebSendGateway
	{
		typedef tuple<SessionPK,MessageTypePtr> QueueType;
		WebSendGateway( WebSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
		void EraseSession( SessionPK id )noexcept;
		void AddExecutionRequest( SessionPK id ){ unique_lock l{_executionRequestMutex}; _executionRequests.emplace( id ); }
		void AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept;
		bool AddAccountSubscription( const string& account, SessionPK sessionId )noexcept;
		void AddMarketDataSubscription( SessionPK sessionId, ContractPK contractId, const flat_set<Proto::Requests::ETickList>& ticks )noexcept;
		tuple<TickerId,flat_set<Proto::Requests::ETickList>> RemoveMarketDataSubscription( ContractPK contractId, SessionPK sessionId, bool haveLock=false )noexcept;
		bool CancelAccountSubscription( const string& account, SessionPK sessionId )noexcept;
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


		void Push( MessageTypePtr pUnion, SessionPK id )noexcept;
		void Push( const vector<MessageTypePtr>& pUnion, SessionPK id )noexcept;
		void PushTick( const vector<Proto::Results::MessageUnion>& messages, ContractPK contractId )noexcept(false);


		void Push( EResults eResults, const ClientKey& key )noexcept;
		void Push( EResults eResults, TickerId ibReqId )noexcept;
		void Push( EResults eResults, function<void(MessageType&)> set )noexcept;

		bool Push( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept;
		//void PushMarketData( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept;

		void PushAccountDownloadEnd( const string& accountNumber )noexcept;
		void Push( const Proto::Results::PortfolioUpdate& pMessage )noexcept;
		//void PushAllocated( unique_ptr<Proto::Results::AccountUpdateMulti> pMessage )noexcept;
		void Push( const Proto::Results::AccountUpdate& accountUpdate, bool haveCallback )noexcept;
		void ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, TickerId tickerId )noexcept;
		void Push( const Proto::Results::CommissionReport& report )noexcept;
		void AccountRequest( const string& accountNumber, function<void(MessageType&)> setMessage )noexcept;
		void AddRequestSessions( SessionPK id, const vector<Proto::Results::EResults>& webSendMessages )noexcept;

		void SetClientSync( sp<TwsClientSync> pClient )noexcept{ DBG0( "WebSendGateway::SetClientSync"sv ); _pClientSync = pClient; }
	private:
		void Push( string&& data, SessionPK sessionId )noexcept;
		ClientKey GetClientRequest( TickerId ibReqId )noexcept{return _requestSession.Find( ibReqId, {0,0} ); }

		void Run()noexcept;
		void HandleRequest( SessionPK sessionId, string&& data )noexcept;

		flat_map<string,boost::container::flat_set<SessionPK>> _accountSubscriptions; std::shared_mutex _accountSubscriptionMutex;
		flat_map<ClientPK,flat_set<TickerId>> _multiRequests; mutable std::mutex _multiRequestMutex;//ie ask for multiple contractDetails
		flat_set<SessionPK> _executionRequests; mutable std::shared_mutex _executionRequestMutex;
		flat_map<string,TimePoint> _canceledAccounts; mutable std::mutex _canceledAccountMutex;//ie ask for multiple contractDetails

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		WebSocket& _webSocket;
		sp<TwsClientSync> _pClientSync;
		UnorderedMapValue<TickerId,uint32> _historicalCrcs; //mutable std::mutex _historicalCacheMutex;
		UnorderedMapValue<TickerId,ClientKey> _requestSession;//single session single call
		Collections::UnorderedMap<Proto::Results::EResults,UnorderedSet<SessionPK>> _requestSessions;//multiple sessions can request item, ie market data.
		flat_map<OrderId,flat_set<SessionPK>> _orderSubscriptions; mutable std::mutex _orderSubscriptionMutex;
		//tuple<TickerId, flat_set<Proto::Requests::ETickList>> MarketDataTicks( ContractPK contractId )noexcept;

		//flat_map<TickerId,ContractPK> _marketTicketContractMap;//todo move _market* to class
		flat_map<ContractPK,flat_map<SessionPK,flat_set<Proto::Requests::ETickList>>> _marketSubscriptions; std::shared_mutex _marketSubscriptionMutex;
		//flat_map<ContractPK,tuple<TickerId,flat_set<Proto::Requests::ETickList>>> _marketSubscriptions; std::mutex _marketSubscriptionsMutex;
	};
}