#include "WebSendGateway.h"
#include "WebCoSocket.h"
#include "WrapperWeb.h"
#include "../../Framework/source/um/UM.h"
#include "../../MarketLibrary/source/TickManager.h"

#define var const auto
#define _client dynamic_cast<TwsClientCache&>(TwsClientSync::Instance())
namespace Jde::Markets::TwsWebSocket
{
	α SetLevel( ELogLevel l )noexcept->void;
	ELogLevel _logLevel{ Logging::TagLevel("web", [](auto l){ SetLevel(l); }) };
	//α LogLevel()noexcept{ return _logLevel; }
	α SetLevel( ELogLevel l )noexcept->void{ _logLevel=l;}

	using Proto::Results::MessageUnion;
	using Proto::Results::MessageValue;
	WebSendGateway::WebSendGateway( WebCoSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept:
		_webSocket{ webSocketParent },
		_pClientSync{ pClientSync }
	{}
	α WebSendGateway::EraseRequestSession( SessionPK sessionId )noexcept->void
	{
		LOG( _logLevel, "({})EraseRequestSession()"sv, sessionId );
		//std::function<void(const EResults&, UnorderedSet<SessionPK>& )> fnctn = ;
		_requestSessions.ForEach( [sessionId]( const EResults& messageId, UnorderedSet<SessionPK>& sessions )
		{
			sessions.erase( sessionId );
			if( messageId==EResults::PositionData )
				sessions.IfEmpty( [&](){ _client.cancelPositions(); });
		} );
	}

	α WebSendGateway::EraseAccountSubscription( SessionPK id, sv account, Handle handle )noexcept->void
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

	bool WebSendGateway::UpdateAccountValue( sv key, sv value, sv currency, sv accountNumber )noexcept
	{
		Proto::Results::AccountUpdate update;
		update.set_account( string{accountNumber} );
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

	bool WebSendGateway::AddAccountSubscription( sv account, SessionPK sessionId )noexcept
	{
		LOG( _logLevel, "({}) Account('{}') subscription for", sessionId, account );
		var haveAccess = WrapperWeb::TryTestAccess( UM::EAccess::Read, account, sessionId );
		if( haveAccess )
		{
			{//RequestAccountUpdates needs it added
				unique_lock l{ _accountSubscriptionMutex };
				_accountSubscriptions.try_emplace( string{account} ).first->second.emplace( sessionId, 0 );
			}
			auto handle = _client.RequestAccountUpdates( account, shared_from_this() );//[p=](sv a, sv b, sv c, sv d){return p->UpdateAccountValue(a,b,c,d);}
			{//save handle
				unique_lock l{ _accountSubscriptionMutex };
				_accountSubscriptions.try_emplace( string{account} ).first->second[sessionId] = handle;
			}
		}
		else
		{
			PushError( -6, format("No access to {}.", account), {{sessionId}} );
		}
		return haveAccess;
	}
	α WebSendGateway::CancelAccountSubscription( sv account, SessionPK sessionId )noexcept->void
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
	α WebSendGateway::Push( EResults type, function<void(MessageType&)> set )noexcept->void
	{
		vector<SessionPK> orphans;
		_requestSessions.Where( type, [&]( const auto& sessionIds )
		{
			sessionIds.ForEach( [&]( const SessionPK& sessionId )
			{
				MessageType msg;
				set( msg ); ASSERT( msg.Value_case()!=MessageType::ValueCase::VALUE_NOT_SET );
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
	α WebSendGateway::Push( Proto::Results::EResults messageId, TickerId ibReqId )noexcept->void
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

	α WebSendGateway::Push( MessageType&& m, SessionPK id )noexcept(false)->void
	{
		_webSocket.AddOutgoing( move(m), id );
	}
	α WebSendGateway::Push( vector<MessageType>&& messages, SessionPK id )noexcept(false)->void
	{
		_webSocket.AddOutgoing( move(messages), id );
	}

	α WebSendGateway::PushTick( const vector<MessageUnion>& messages, ContractPK contractId )noexcept(false)->void
	{
		unique_lock l{ _marketSubscriptionMutex };
		if( auto p=_marketSubscriptions.find(contractId); p!=_marketSubscriptions.end() )
		{
			if( p->second.empty() )
			{
				_marketSubscriptions.erase( p );
				THROW( "Could not find any market subscriptions. contractId={}", contractId );
			}
			typedef flat_map<SessionPK,flat_set<Proto::Requests::ETickList>>::iterator X;
			for( X pSession = p->second.begin(); pSession != p->second.end(); )
			{
				try
				{
					_webSocket.AddOutgoing( messages, pSession->first );
					++pSession;
				}
				catch( const IException& )
				{
					LOG( _logLevel, "({}) Removing for contract id='{}'"sv, pSession->first, contractId );
					pSession = p->second.erase( pSession );
				}
			}
		}
		else
			THROW( "Could not find any market subscriptions." );
	}

	α WebSendGateway::PushError( int errorCode, sv errorString, TickerId id )noexcept->void
	{
		if( id>0 )
		{
			var key = GetClientRequest( id );
			if( key.SessionId )
				Push( IBException{errorString, errorCode, -1}, key );
			else
				DBG( "Could not find session for error req:  '{}'."sv, id );
		}
		else if( errorCode==504 )
		{
			vector<TickerId> lostIds;
			_requestSession.ForEach( [errorCode, &errorString, pThis=shared_from_this(),&lostIds](auto tickerId, const auto& key)
			{
				try
				{
					pThis->Push( IBException{errorString, errorCode, -1}, key );
				}
				catch( const IException& )
				{
					lostIds.push_back( tickerId );
				}
			});
			for_each( lostIds.begin(), lostIds.end(), [&](auto id){_requestSession.erase(id);} );
		}
	}
	α WebSendGateway::PushError( int errorCode, str errorString, const ClientKey& key )noexcept(false)->void
	{
		auto pError = make_unique<Proto::Results::Error>(); pError->set_request_id(key.ClientId); pError->set_code(errorCode); pError->set_message(errorString);
		MessageUnion msg; msg.set_allocated_error( pError.release() );
		Push( move(msg), key.SessionId );
	}

	α WebSendGateway::AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept->void
	{
		unique_lock l{ _orderSubscriptionMutex };

		auto& sessions = _orderSubscriptions.try_emplace( orderId ).first->second;
		sessions.emplace( sessionId );
	}

	bool WebSendGateway::Push( TickerId id, function<void(MessageType&, ClientPK)> set )noexcept(false)
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

	α WebSendGateway::AddMarketDataSubscription( SessionPK sessionId, ContractPK contractId, const flat_set<Proto::Requests::ETickList>& ticks )noexcept->void
	{
		unique_lock l{ _marketSubscriptionMutex };
		auto& contractSubscriptions = _marketSubscriptions.try_emplace( contractId ).first->second;
		contractSubscriptions[sessionId] = ticks;
	}

	α WebSendGateway::ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, ReqId reqId )noexcept->void
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
					auto pMessage = new MessageValue(); pMessage->set_type( Proto::Results::EResults::MultiEnd ); pMessage->set_int_value( clientKey.ClientId );
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

	bool WebSendGateway::AccountRequest( str accountNumber, function<void(MessageType&)> setMessage )noexcept
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
	bool WebSendGateway::PortfolioUpdate( const Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept
	{
		return AccountRequest( porfolioUpdate.account_number(), [&porfolioUpdate](MessageType& msg){msg.set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate});} );
	}

	α WebSendGateway::AccountDownloadEnd( sv accountNumber )noexcept->void
	{
		MessageValue value; value.set_type( Proto::Results::EResults::AccountDownloadEnd ); value.set_string_value( string{accountNumber} );
		AccountRequest( string{accountNumber}, [&value](MessageType& msg)mutable{msg.set_allocated_message( new MessageValue{value});} );
	}

	α WebSendGateway::Push( const Proto::Results::CommissionReport& report )noexcept->void
	{
		std::shared_lock<std::shared_mutex> l( _executionRequestMutex );
		for( var sessionId : _executionRequests )
		{
			MessageType msg; msg.set_allocated_commission_report( new Proto::Results::CommissionReport{report} );
			Push( move(msg), sessionId );
		}
	}

	α WebSendGateway::Push( Proto::Results::EResults messageId, const ClientKey& key )noexcept(false)->void
	{
		auto pMessage = new MessageValue(); pMessage->set_int_value( key.ClientId ); pMessage->set_type( messageId );
		MessageType msg; msg.set_allocated_message( pMessage );
		Push( move(msg), key.SessionId );
	}
}