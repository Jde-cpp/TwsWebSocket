#pragma once
#include <boost/container/flat_map.hpp>
#include <boost/core/noncopyable.hpp>
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../MarketLibrary/source/wrapper/WrapperLog.h"
#include "../../MarketLibrary/source/types/IBException.h"

namespace Jde::Markets{ struct IBException;}
namespace Jde::Markets::TwsWebSocket
{
	struct WebCoSocket;
	struct WebSendGateway final: std::enable_shared_from_this<WebSendGateway>, boost::noncopyable, IAccountUpdateHandler //TODO not a worker, WebSendGateway
	{
		typedef tuple<SessionPK,MessageTypePtr> QueueType;
		WebSendGateway( WebCoSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
		void EraseRequestSession( SessionPK id )noexcept;
		void EraseAccountSubscription( SessionPK id, sv account={}, Handle handle=0 )noexcept;
		void EraseSession( SessionPK id )noexcept;
		void AddExecutionRequest( SessionPK id ){ unique_lock l{_executionRequestMutex}; _executionRequests.emplace( id ); }
		void AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept;
		bool AddAccountSubscription( sv account, SessionPK sessionId )noexcept;
		bool UpdateAccountValue( sv key, sv value, sv currency, sv accountName )noexcept override;
		void AddMarketDataSubscription( SessionPK sessionId, ContractPK contractId, const flat_set<Proto::Requests::ETickList>& ticks )noexcept;
		tuple<TickerId,flat_set<Proto::Requests::ETickList>> RemoveMarketDataSubscription( ContractPK contractId, SessionPK sessionId, bool haveLock=false )noexcept;
		void CancelAccountSubscription( sv account, SessionPK sessionId )noexcept;
		void AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key );

		TickerId AddRequestSession( const ClientKey& key )noexcept{ const auto ib = _pClientSync->RequestId(); _requestSession.emplace(ib, key); return ib; }
		TickerId AddRequestSession( const ClientKey& key, TickerId ib )noexcept{ auto value = ib; if( value ) _requestSession.emplace(ib, key); else value = AddRequestSession(key); return value; }
		TickerId RequestFind( const ClientKey& key )const noexcept;
		void RequestErase( TickerId id )noexcept{ _requestSession.erase(id); }

		bool HasHistoricalRequest( TickerId id )const noexcept{ return _historicalCrcs.Has(id); }

		void TryPush( const std::exception& e, const ClientKey& key )noexcept{ TRY(Push(e, key)); }
		void Push( const std::exception& e, const ClientKey& key )noexcept(false){ PushError( -1, e.what(), key );}
		void Push( const IBException& e, const ClientKey& key )noexcept(false){ PushError( e.ErrorCode, e.what(), key );}
		void PushError( int errorCode, sv errorString, TickerId id )noexcept;
		void PushError( int errorCode, str errorString, const ClientKey& key )noexcept(false);

		void Push( MessageType&& pUnion, SessionPK id )noexcept(false);
		void Push( vector<MessageType>&& pUnion, SessionPK id )noexcept(false);
		void PushTick( const vector<Proto::Results::MessageUnion>& messages, ContractPK contractId )noexcept(false);

		void Push( EResults eResults, const ClientKey& key )noexcept(false);
		void Push( EResults eResults, TickerId ibReqId )noexcept;
		void Push( EResults eResults, function<void(MessageType&)> set )noexcept;
		void Push( EResults eResults, function<void(MessageType&, SessionPK)> set )noexcept;

		bool Push( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept(false);
		optional<bool> TryPush( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept{ return Try<bool>( [&]()->bool{return this->Push(id,set);} ); }

		void AccountDownloadEnd( sv accountNumber )noexcept override;
		bool PortfolioUpdate( const Proto::Results::PortfolioUpdate& pMessage )noexcept override;
		void ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, TickerId tickerId )noexcept;
		void Push( const Proto::Results::CommissionReport& report )noexcept;
		bool AccountRequest( str accountNumber, function<void(MessageType&)> setMessage )noexcept;
		void AddRequestSessions( SessionPK id, const vector<Proto::Results::EResults>& webSendMessages )noexcept;

		void SetClientSync( sp<TwsClientSync> pClient )noexcept{ DBG( "WebSendGateway::SetClientSync"sv ); _pClientSync = pClient; }
	private:
		void Push( string&& data, SessionPK sessionId )noexcept;
		ClientKey GetClientRequest( TickerId ibReqId )noexcept{return _requestSession.Find( ibReqId ).value_or( ClientKey{} ); }

		void Run()noexcept;
		void HandleRequest( SessionPK sessionId, string&& data )noexcept;

		flat_map<string,flat_map<SessionPK,Handle>> _accountSubscriptions; std::shared_mutex _accountSubscriptionMutex; unique_ptr<unique_lock<shared_mutex>> _accountSubscriptionPtr;
		flat_map<ClientPK,flat_set<TickerId>> _multiRequests; mutable std::mutex _multiRequestMutex;//ie ask for multiple contractDetails
		flat_set<SessionPK> _executionRequests; mutable std::shared_mutex _executionRequestMutex;

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		WebCoSocket& _webSocket;
		sp<TwsClientSync> _pClientSync;
		UnorderedMapValue<TickerId,uint32> _historicalCrcs; //mutable std::mutex _historicalCacheMutex;
		UnorderedMapValue<TickerId,ClientKey> _requestSession;//single session single call
		Collections::UnorderedMap<Proto::Results::EResults,UnorderedSet<SessionPK>> _requestSessions;//multiple sessions can request item, ie market data.
		flat_map<OrderId,flat_set<SessionPK>> _orderSubscriptions; mutable std::mutex _orderSubscriptionMutex;

		flat_map<ContractPK,flat_map<SessionPK,flat_set<Proto::Requests::ETickList>>> _marketSubscriptions; std::shared_mutex _marketSubscriptionMutex;
	};
}