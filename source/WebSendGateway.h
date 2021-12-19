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
		typedef tuple<SessionPK,MessageTypePtr> TQueue;
		WebSendGateway( WebCoSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept;
		α Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
		α EraseRequestSession( SessionPK id )noexcept->void;
		α EraseAccountSubscription( SessionPK id, sv account={}, Handle handle=0 )noexcept->void;
		α EraseSession( SessionPK id )noexcept->void;
		α AddExecutionRequest( SessionPK id ){ unique_lock l{_executionRequestMutex}; _executionRequests.emplace( id ); }
		α AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept->void;
		α AddAccountSubscription( string ibId, SessionPK sessionId )noexcept->void;
		α UpdateAccountValue( sv key, sv value, sv currency, sv accountName )noexcept->bool override;
		α AddMarketDataSubscription( SessionPK sessionId, ContractPK contractId, const flat_set<Proto::Requests::ETickList>& ticks )noexcept->void;
		α RemoveMarketDataSubscription( ContractPK contractId, SessionPK sessionId, bool haveLock=false )noexcept->tuple<TickerId,flat_set<Proto::Requests::ETickList>>;
		α CancelAccountSubscription( sv account, SessionPK sessionId )noexcept->void;
		α AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key )->void;

		α AddRequestSession( const ClientKey& key )noexcept{ const auto ib = _pClientSync->RequestId(); _requestSession.emplace(ib, key); return ib; }
		α AddRequestSession( const ClientKey& key, TickerId ib )noexcept{ auto value = ib; if( value ) _requestSession.emplace(ib, key); else value = AddRequestSession(key); return value; }
		α RequestFind( const ClientKey& key )const noexcept->TickerId;
		α RequestErase( TickerId id )noexcept{ _requestSession.erase(id); }

		α HasHistoricalRequest( TickerId id )const noexcept{ return _historicalCrcs.Has(id); }

		α Push( string message, const IException& e, const ClientKey& key )noexcept->void{ PushError( move(message), key, e.Code ); }
//		α Push( const IException& e, const ClientKey& key )noexcept->void{ PushError( e.What(), key, e.Id() );}
		α PushError( string errorString, const ClientKey& key, int errorCode=0 )noexcept->void;

		//α Push( string message, IException& e, const ClientKey& key )noexcept(false)->void{ PushError( e.MessageId, move(message), key );}
		//α PushError( int errorCode, string errorString, TickerId id )noexcept->void;

		α Push( MessageType&& pUnion, SessionPK id )noexcept(false)->void;
		α Push( vector<MessageType>&& pUnion, SessionPK id )noexcept(false)->void;
		α PushTick( const vector<Proto::Results::MessageUnion>& messages, ContractPK contractId )noexcept(false)->void;

		α Push( EResults eResults, const ClientKey& key )noexcept(false)->void;
		α Push( EResults eResults, TickerId ibReqId )noexcept->void;
		//α Push( EResults eResults, function<void(MessageType&)> set )noexcept->void;
		α Push( EResults eResults, function<void(MessageType&, SessionPK)> set )noexcept->void;

		α Push( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept(false)->bool;
		α TryPush( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept{ return Try<bool>( [&]()->bool{return this->Push(id,set);} ); }

		α AccountDownloadEnd( sv accountNumber )noexcept->void override;
		α PortfolioUpdate( const Proto::Results::PortfolioUpdate& pMessage )noexcept->bool override;
		α ContractDetails( up<Proto::Results::ContractDetailsResult> pDetails, TickerId tickerId )noexcept->void;
		α Push( const Proto::Results::CommissionReport& report )noexcept->void;
		α AccountRequest( str accountNumber, function<void(MessageType&)> setMessage )noexcept->bool;
		α AddRequestSessions( SessionPK id, const vector<Proto::Results::EResults>& webSendMessages )noexcept->void;

		α SetClientSync( sp<TwsClientSync> pClient )noexcept->void{ DBG( "WebSendGateway::SetClientSync"sv ); _pClientSync = pClient; }
	private:
		α Push( string&& data, SessionPK sessionId )noexcept->void;
		α GetClientRequest( TickerId ibReqId )noexcept{return _requestSession.Find( ibReqId ).value_or( ClientKey{} ); }

		α Run()noexcept->void;
		α HandleRequest( SessionPK sessionId, string&& data )noexcept->void;

		flat_map<string,flat_map<SessionPK,Handle>> _accountSubscriptions; std::shared_mutex _accountSubscriptionMutex; up<unique_lock<shared_mutex>> _accountSubscriptionPtr;
		flat_map<ClientPK,flat_set<TickerId>> _multiRequests; mutable std::mutex _multiRequestMutex;//ie ask for multiple contractDetails
		flat_set<SessionPK> _executionRequests; mutable std::shared_mutex _executionRequestMutex;

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<TQueue> _queue;
		WebCoSocket& _webSocket;
		sp<TwsClientSync> _pClientSync;
		UnorderedMapValue<TickerId,uint32> _historicalCrcs; //mutable std::mutex _historicalCacheMutex;
		UnorderedMapValue<TickerId,ClientKey> _requestSession;//single session single call
		Collections::UnorderedMap<Proto::Results::EResults,UnorderedSet<SessionPK>> _requestSessions;//multiple sessions can request item, ie market data.
		flat_map<OrderId,flat_set<SessionPK>> _orderSubscriptions; mutable std::mutex _orderSubscriptionMutex;

		flat_map<ContractPK,flat_map<SessionPK,flat_set<Proto::Requests::ETickList>>> _marketSubscriptions; std::shared_mutex _marketSubscriptionMutex;
	};
}