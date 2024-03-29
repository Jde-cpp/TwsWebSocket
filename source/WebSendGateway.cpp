﻿#include "WebSendGateway.h"
#include "WebCoSocket.h"
#include "WrapperWeb.h"
#include <jde/markets/types/proto/ResultsMessage.h>
#include "../../Framework/source/um/UM.h"
#include "../../MarketLibrary/source/TickManager.h"
#include "../../MarketLibrary/source/data/Accounts.h"
#include "../../MarketLibrary/source/OrderManager.h"

#define var const auto
#define _client dynamic_cast<TwsClient&>(TwsClientSync::Instance())
namespace Jde::Markets::TwsWebSocket
{
	static const LogTag& _logLevel = Logging::TagLevel( "app-toWeb" );
	α Instance()noexcept->WebSendGateway&{ return *WebCoSocket::Instance()->WebSend(); }
	using Proto::Results::MessageUnion;
	using Proto::Results::MessageValue;
	WebSendGateway::WebSendGateway( WebCoSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept:
		_webSocket{ webSocketParent },
		_pClientSync{ pClientSync }
	{}

	flat_map<SessionPK,flat_set<::OrderId>> _sessionOrders; std::mutex _sessionOrderMutex;
	α WebSendGateway::AddOrder( ::OrderId id, SessionPK s )noexcept->void
	{
		lock_guard _{ _sessionOrderMutex };
		_sessionOrders.try_emplace( s ).first->second.emplace( id );
	}
	α CheckPlaceOrderResult( SessionPK s, ::OrderId orderId )->bool//if order created by client, just send notification on placeOrder result.
	{
		bool result = true;
		lock_guard _{ _sessionOrderMutex };
		if( auto p = _sessionOrders.find(s); p!=_sessionOrders.end() && p->second.find(orderId)!=p->second.end() )
		{
			p->second.erase( orderId );
			if( p->second.empty() )
				_sessionOrders.erase( p );
		}
		else
			result = false;

		return result;
	}
	α WebSendGateway::OnOrderException( string account_, sp<const IBException> e_ )noexcept->Task
	{
		co_await WebCoSocket::CoForEachSession( [a=move(account_), e=e_ ]( const SessionInfo& x )
		{
			if( Accounts::CanRead(a, x.UserId) && !CheckPlaceOrderResult(x.SessionId, e->RequestId) )
				WebCoSocket::Send( ToMessage(e->What(), e->RequestId, (int)e->Code), x.SessionId );
		} );
	}

	α WebSendGateway::OnOrderChange( sp<const MyOrder> pOrder, sp<const Markets::Contract> pContract, sp<const OrderStatus> pStatus, sp<const ::OrderState> pState )noexcept->Task
	{
		co_await WebCoSocket::CoForEachSession( [=]( const SessionInfo& x )
		{
			if( Accounts::CanRead(pOrder->account, x.UserId) && !CheckPlaceOrderResult(x.SessionId, pOrder->orderId) )
				WebCoSocket::Send( ToMessage(pOrder->orderId, pStatus, pState), x.SessionId );
		} );
	}

	α WebSendGateway::EraseRequestSession( SessionPK sessionId )noexcept->void
	{
		LOG( "({})EraseRequestSession()"sv, sessionId );
		_requestSessions.ForEach( [sessionId]( const EResults& messageId, UnorderedSet<SessionPK>& sessions )
		{
			sessions.erase( sessionId );
			if( messageId==EResults::PositionData )
				sessions.IfEmpty( [&](){ _client.cancelPositions(); });
		} );
	}

	α WebSendGateway::EraseAccountSubscription( SessionPK id, str account, Handle handle )noexcept->void
	{
		DBG( "({})EraseAccountSubscription( '{}', '{}' )"sv, id, account, handle );

		flat_set<Handle> orphans;
		if( handle )
			orphans.emplace( handle );
		std::unique_lock<std::shared_mutex> l( _accountSubscriptionMutex );
		for( auto pAccountSessionIds = _accountSubscriptions.begin(); pAccountSessionIds!=_accountSubscriptions.end();  )
		{
			auto erase = false;
			if( account.size() && pAccountSessionIds->first==account )
			{
				auto& sessionIds = pAccountSessionIds->second;
				if( auto p = sessionIds.find(id); p!=sessionIds.end() )
				{
					orphans.emplace( p->second );
					sessionIds.erase( p );
					erase = sessionIds.size()==0;
				}
				break;
			}
			pAccountSessionIds =  erase ? _accountSubscriptions.erase( pAccountSessionIds ) : std::next( pAccountSessionIds );
		}
		std::for_each( orphans.begin(), orphans.end(), [account]( var& idHandle ){ _client.CancelAccountUpdates(account, idHandle); } );
	}

	α WebSendGateway::EraseSession( SessionPK id )noexcept->void
	{
		DBG( "Removing session '{}'"sv, id );
		EraseRequestSession( id );
		EraseAccountSubscription( id );
		TickManager::CancelProto( (uint32)id, 0, 0 );
	}

	α WebSendGateway::AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key )->void
	{
		{
			unique_lock l{_multiRequestMutex};
			_multiRequests.emplace( key.ClientId, ids );
		}
		std::for_each( ids.begin(), ids.end(), [this, &key](auto id){ _requestSession.emplace(id, key); } );
	}

	α WebSendGateway::AddRequestSessions( SessionPK id, const vector<EResults>& webSendMessages )noexcept->void//todo arg forwarding.
	{
		auto afterInsert = [id](UnorderedSet<SessionPK>& values){values.emplace(id);};
		for( var sendMessage : webSendMessages )
			_requestSessions.Insert( afterInsert, sendMessage, sp<UnorderedSet<SessionPK>>{new UnorderedSet<SessionPK>{}} );
	}

	α WebSendGateway::UpdateAccountValue( sv key, sv value, sv currency, str accountNumber )noexcept->bool
	{
		Proto::Results::AccountUpdate update;
		update.set_account( accountNumber );
		update.set_key( string{key} );
		update.set_value( string{value} );
		update.set_currency( string{currency} );

		bool haveSubscription = false;
		std::shared_lock<std::shared_mutex> l( _accountSubscriptionMutex );
		if( var pAccountNumberSessions = _accountSubscriptions.find( string{accountNumber} ); pAccountNumberSessions!=_accountSubscriptions.end() )
		{
			map<SessionPK,Handle> orphans;
			for( var& [sessionId,handle] : pAccountNumberSessions->second )
			{
				MessageType msg; msg.set_allocated_account_update( new Proto::Results::AccountUpdate{update} );
				var s2 = sessionId;
				if( TRY(Push(move(msg), s2)) )
					haveSubscription = true;
				else
					orphans.emplace( sessionId, handle );
			}
			l.unlock();
			for_each( orphans.begin(), orphans.end(), [&]( auto idHandle ){EraseAccountSubscription(idHandle.first, accountNumber, idHandle.second);} );
		}
		return haveSubscription;
	}

	α WebSendGateway::AddAccountSubscription( string ibId, SessionPK sessionId )noexcept->void
	{
		LOG( "({}) Account('{}') subscription for", sessionId, ibId );
		if( var userId=WebCoSocket::TryUserId(sessionId); Accounts::CanRead(ibId, userId) )
		{
			{//RequestAccountUpdates needs it added
				unique_lock l{ _accountSubscriptionMutex };
				_accountSubscriptions.try_emplace( ibId ).first->second.emplace( sessionId, 0 );
			}
			auto handle = _client.RequestAccountUpdates( ibId, shared_from_this() );//[p=](sv a, sv b, sv c, sv d){return p->UpdateAccountValue(a,b,c,d);}
			{//save handle
				unique_lock l{ _accountSubscriptionMutex };
				_accountSubscriptions.try_emplace( move(ibId) ).first->second[sessionId] = handle;
			}
		}
		else
			Push( format("No access to {}.", move(ibId)), Exception{SRCE_CUR, _logLevel.Level, "No access to {}.", move(ibId)}, {{sessionId}} );
	}
	α WebSendGateway::CancelAccountSubscription( str account, SessionPK sessionId )noexcept->void
	{
		DBG( "Account('{}') unsubscribe for sessionId='{}'"sv, account, sessionId );
		EraseAccountSubscription( sessionId, account );
	}

	TickerId WebSendGateway::RequestFind( const ClientKey& key )const noexcept
	{
		auto values = _requestSession.Find( [&key]( const auto& value ){ return value==key; } );
		return values.size() ? values.begin()->first : 0;
	}

	α WebSendGateway::Push( EResults type, function<void(MessageType&, SessionPK)> set )noexcept->void
	{
		vector<SessionPK> orphans;
		_requestSessions.Where( type, [&]( const auto& sessionIds )
		{
			sessionIds.ForEach( [&]( const SessionPK& sessionId )
			{
				MessageType msg;
				set( msg, sessionId ); ASSERT( msg.Value_case()!=MessageType::ValueCase::VALUE_NOT_SET );
				try
				{
					Push( move(msg), sessionId );
				}
				catch( const IException& )
				{
					orphans.emplace_back( sessionId );
				}
			});
		});
		for_each( orphans.begin(), orphans.end(), [this](auto id){ EraseRequestSession( id ); } );
	}

	α WebSendGateway::Push( EResults messageId, TickerId ibReqId )noexcept->void
	{
		var clientKey = GetClientRequest( ibReqId );
		if( clientKey.SessionId )
		{
			auto pMessage = new MessageValue(); pMessage->set_int_value( clientKey.ClientId ); pMessage->set_type( messageId );
			MessageType msg; msg.set_allocated_message( pMessage );
			Push( move(msg), clientKey.SessionId );
		}
		else
			DBG( "Could not find session for messageId:  '{}' req:  '{}'."sv, messageId, ibReqId );
	}

	α WebSendGateway::Push( MessageType&& m, SessionPK id )noexcept->void
	{
		_webSocket.AddOutgoing( move(m), id );
	}

	α WebSendGateway::PushS( MessageType&& m, SessionPK id )noexcept->void
	{
		Instance().Push( move(m), id );
	}
	α WebSendGateway::PushErrorS( string errorString, const ClientKey& key, int errorCode )noexcept->void
	{
		Instance().PushError( move(errorString), key, errorCode );
	}

	α WebSendGateway::PushError( string errorString, const ClientKey& key, int errorCode )noexcept->void
	{
		auto pError = mu<Proto::Results::Error>(); pError->set_request_id(key.ClientId); pError->set_code( errorCode==0 ? Calc32RunTime(errorString) : errorCode ); pError->set_message( move(errorString) );
		MessageUnion msg; msg.set_allocated_error( pError.release() );
		try
		{
			Push( move(msg), key.SessionId );
		}
		catch( IException& )
		{}
	}

	α WebSendGateway::AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept->void
	{
		unique_lock l{ _orderSubscriptionMutex };

		auto& sessions = _orderSubscriptions.try_emplace( orderId ).first->second;
		sessions.emplace( sessionId );
	}

	α WebSendGateway::Push( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept(false)->bool
	{
		var clientKey = GetClientRequest( id );
		if( clientKey.SessionId )
		{
			MessageType msg;
			set( msg, clientKey.ClientId );
			Push( move(msg), clientKey.SessionId );
		}
		return clientKey.SessionId;
	}

	α WebSendGateway::ContractDetails( up<Proto::Results::ContractDetailsResult> pDetails, ReqId reqId )noexcept->void
	{
		var clientKey = _requestSession.Find( reqId ).value_or( ClientKey{} );
		if( !clientKey.SessionId )
		{
			DBG( "Could not find session for ContractDetailsEnd req:  '{}'."sv, reqId );
			return;
		}
		bool multi=false;//web call requested multiple contract details
		optional<MessageType> pComplete;
		{
			unique_lock l{_multiRequestMutex};
			auto pMultiRequests = _multiRequests.find( clientKey.ClientId );
			multi = pMultiRequests!=_multiRequests.end();
			if( multi )
			{
				auto& reqIds = pMultiRequests->second;
				reqIds.erase( reqId );
				if( reqIds.size()==0 )
				{
					_multiRequests.erase( pMultiRequests );
					auto pMessage = new MessageValue(); pMessage->set_type( EResults::MultiEnd ); pMessage->set_int_value( clientKey.ClientId );
					pComplete = MessageType{};  pComplete->set_allocated_message( pMessage );
				}
			}
		}
		pDetails->set_request_id( clientKey.ClientId );
		MessageType msg; msg.set_allocated_contract_details( pDetails.release() );
		if( pComplete )
			TRY( Push({msg,*pComplete}, clientKey.SessionId) );
		else
			TRY( Push(move(msg), clientKey.SessionId) );
		if( pComplete || !multi )
			_requestSession.erase( reqId );
	}

	α WebSendGateway::AccountRequest( str accountNumber, function<void(MessageType&)> setMessage )noexcept->bool
	{
		std::unique_lock<std::shared_mutex> l3( _accountSubscriptionMutex );
		var pAccountNumberSessions = _accountSubscriptions.find(accountNumber);
		bool haveSubscription = pAccountNumberSessions!=_accountSubscriptions.end();
		if(  haveSubscription )
		{
			vector<SessionPK> orphans;
			for( var& [sessionId,handle] : pAccountNumberSessions->second )
			{
				MessageType msg;
				setMessage( msg );
				string key = msg.has_portfolio_update() ? std::to_string( msg.portfolio_update().contract().id() ) : string{};
				if( !Try( [&, id=sessionId]{ Push(move(msg), id);} ) )
					orphans.push_back( sessionId );
			}
			l3.unlock();
			if( orphans.size() )
			{
				for_each( orphans.begin(), orphans.end(), [this](auto id){ EraseRequestSession( id ); } );
				haveSubscription = _accountSubscriptions.find( accountNumber )==_accountSubscriptions.end();
			}
		}
		return haveSubscription;
	}
	α WebSendGateway::PortfolioUpdate( const Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept->bool
	{
		return AccountRequest( porfolioUpdate.account_number(), [&porfolioUpdate](MessageType& msg){msg.set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate});} );
	}

	α WebSendGateway::AccountDownloadEnd( sv accountNumber )noexcept->void
	{
		MessageValue value; value.set_type( EResults::AccountDownloadEnd ); value.set_string_value( string{accountNumber} );
		AccountRequest( string{accountNumber}, [&value](MessageType& msg)mutable{msg.set_allocated_message( new MessageValue{value});} );
	}

	α WebSendGateway::Push( const vector<MessageType>& m, SessionPK s )noexcept(false)->void{ _webSocket.AddOutgoing(m, s); }
	α WebSendGateway::Push( vector<MessageType>&& m, SessionPK s )noexcept->void{ _webSocket.AddOutgoing(move(m), s); }

	α WebSendGateway::Push( const Proto::Results::CommissionReport& report )noexcept->void
	{
		std::shared_lock<std::shared_mutex> l( _executionRequestMutex );
		for( var sessionId : _executionRequests )
		{
			MessageType msg; msg.set_allocated_commission_report( new Proto::Results::CommissionReport{report} );
			TryPush( move(msg), sessionId );
		}
	}

	α WebSendGateway::PushS( EResults eResults, const ClientKey& c )noexcept->void
	{
		Instance().Push( eResults, c );
	}

	α WebSendGateway::Push( EResults r, const ClientKey& c )noexcept->void
	{
		Push( ToMessage(r, c.ClientId), c.SessionId );
	}
}