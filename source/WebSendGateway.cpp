#include "WebSendGateway.h"
#include "WebCoSocket.h"
#include "WrapperWeb.h"
#include "../../Framework/source/um/UM.h"
#include "../../MarketLibrary/source/TickManager.h"

#define var const auto
#define _client dynamic_cast<TwsClientCache&>(TwsClientSync::Instance())
namespace Jde::Markets::TwsWebSocket
{
	using Proto::Results::MessageUnion;
	using Proto::Results::MessageValue;
	WebSendGateway::WebSendGateway( WebCoSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept:
//		_queue{},
		_webSocket{ webSocketParent },
		_pClientSync{ pClientSync }
	{
		DBG( "WebSendGateway::WebSendGateway"sv );
	}
	void WebSendGateway::EraseRequestSession( SessionPK sessionId )noexcept
	{
		DBG( "EraseRequestSession( {} )"sv, sessionId );
		std::function<void(const EResults&, UnorderedSet<SessionPK>& )> fnctn = [sessionId]( const EResults& messageId, UnorderedSet<SessionPK>& sessions )
		{
			sessions.erase( sessionId );
			if( messageId==EResults::PositionData )
				sessions.IfEmpty( [&](){ _client.cancelPositions(); });
		};
		_requestSessions.ForEach( fnctn );
	}

	void WebSendGateway::EraseAccountSubscription( SessionPK id, sv account, Handle handle )noexcept
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

	void WebSendGateway::EraseSession( SessionPK id )noexcept
	{
		DBG( "Removing session '{}'"sv, id );
		EraseRequestSession( id );
		EraseAccountSubscription( id );
		TickManager::CancelProto( (uint32)id, 0, 0 );
	}

	void WebSendGateway::AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key )
	{
		{
			unique_lock l{_multiRequestMutex};
			_multiRequests.emplace( key.ClientId, ids );
		}
		std::for_each( ids.begin(), ids.end(), [this, &key](auto id){ _requestSession.emplace(id, key); } );
	}

	void WebSendGateway::AddRequestSessions( SessionPK id, const vector<EResults>& webSendMessages )noexcept//todo arg forwarding.
	{
		auto afterInsert = [id](UnorderedSet<SessionPK>& values){values.emplace(id);};
		for( var sendMessage : webSendMessages )
			_requestSessions.Insert( afterInsert, sendMessage, sp<UnorderedSet<SessionPK>>{new UnorderedSet<SessionPK>{}} );
	}

	bool WebSendGateway::UpdateAccountValue( sv key, sv value, sv currency, sv accountNumber )noexcept
	{
		//var haveCallback = WrapperLog::updateAccountValue2( key, value, currency, accountName );
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
			for_each( orphans.begin(), orphans.end(), [&](auto idHandle ){ EraseAccountSubscription( idHandle.first, accountNumber, idHandle.second ); } );
		}
		return haveSubscription;
	}

	bool WebSendGateway::AddAccountSubscription( sv account, SessionPK sessionId )noexcept
	{
		DBG( "({})Account('{}') subscription for sessionId='{}'"sv, sessionId, account );
		var haveAccess = WrapperWeb::TryTestAccess( UM::EAccess::Read, account, sessionId );
		//DBG( "A"sv );
		if( haveAccess )
		{
			{//RequestAccountUpdates needs it added
				unique_lock l{ _accountSubscriptionMutex };
				//DBG( "A.1"sv );
				_accountSubscriptions.try_emplace( string{account} ).first->second.emplace( sessionId, 0 );
			}
			auto handle = _client.RequestAccountUpdates( account, shared_from_this() );//[p=](sv a, sv b, sv c, sv d){return p->UpdateAccountValue(a,b,c,d);}
			//DBG( "B"sv );
			{//save handle
				unique_lock l{ _accountSubscriptionMutex };
				_accountSubscriptions.try_emplace( string{account} ).first->second[sessionId] = handle;
			}
		}
		else
		{
			//DBG( "A.2"sv );
			PushError( -6, format("No access to {}.", account), {{sessionId}} );
		}
		//DBG( "C"sv );
		return haveAccess;
	}
	void WebSendGateway::CancelAccountSubscription( sv account, SessionPK sessionId )noexcept
	{
		DBG( "Account('{}') unsubscribe for sessionId='{}'"sv, account, sessionId );
		EraseAccountSubscription( sessionId, account );
	}
/*	tuple<TickerId, flat_set<Proto::Requests::ETickList>> WebSendGateway::MarketDataTicks( ContractPK contractId )noexcept
	{
		flat_set<Proto::Requests::ETickList> ticks;

		TickerId reqId{0};
		if( auto pContractSubscriptions = _marketSessionSubscriptions.find( contractId ); pContractSubscriptions!=_marketSessionSubscriptions.end() )
		{
			ASSERT_DESC( pContractSubscriptions->second.size(), "Should have been removed/added in AddMarketDataSubscription" );
			for( var& sessionTicks : pContractSubscriptions->second )
			{
				for( var tick : sessionTicks.second )
					ticks.emplace( tick );
			}
			auto pExisting = _marketSubscriptions.find( contractId );
			if( pExisting==_marketSubscriptions.end()  )
				_marketSubscriptions.emplace( contractId, make_tuple(reqId = _client.RequestId(), ticks) );
			else
			{
				if( get<1>(pExisting->second)!=ticks )
				{
					_client.cancelMktData( get<0>(pExisting->second) );//todo move this & probably _marketSubscriptions to TwsSendWorker.
					pExisting->second = make_tuple( reqId = _client.RequestId(), ticks );
				}
				else
				{
					ostringstream os{"["};
					for_each( ticks.begin(), ticks.end(), [&os](var tick){ os << tick << ",";} );
					os << "]==[";
					var y = get<1>(pExisting->second);
					for_each( y.begin(), y.end(), [&os](var tick){ os << tick << ",";} );
					os << "]";
					DBG( "ticks equal = {}"sv, os.str() );
				}
			}
		}
		else if( var p = _marketSubscriptions.find(contractId); p!=_marketSubscriptions.end() )//if need to cancel...
			reqId = get<0>( p->second );

		return make_tuple( reqId, ticks );
	}

	tuple<TickerId,flat_set<Proto::Requests::ETickList>> WebSendGateway::AddMarketDataSubscription( ContractPK contractId, flat_set<Proto::Requests::ETickList>&& ticks, SessionPK sessionId )noexcept
	{
		std::unique_lock l{ _marketSubscriptionsMutex };
		auto [pRequestSessions,inserted] = _marketSessionSubscriptions.try_emplace( contractId );
		if( auto pSessionTicks = inserted ? pRequestSessions->second.end() : pRequestSessions->second.find( sessionId ); pSessionTicks!=pRequestSessions->second.end() )
			pSessionTicks->second = move( ticks );
		else
			pRequestSessions->second.emplace( sessionId, move(ticks) );
		var result = MarketDataTicks( contractId );
		_marketTicketContractMap.emplace( get<0>(result), contractId );

		return result;
	}
	tuple<TickerId,flat_set<Proto::Requests::ETickList>> WebSendGateway::RemoveMarketDataSubscription( ContractPK contractId, SessionPK sessionId, bool haveLock )noexcept
	{
		auto pLock = haveLock ? up<std::unique_lock<std::mutex>>{} : make_unique<std::unique_lock<std::mutex>>( _marketSubscriptionsMutex );
	 	if( auto pRequestSessions = _marketSessionSubscriptions.find( contractId ); pRequestSessions!=_marketSessionSubscriptions.end() )//does session have any for contract?
	 	{
	 		pRequestSessions->second.erase( sessionId );
	 		if( !pRequestSessions->second.size() )
			{
				_marketSessionSubscriptions.erase( pRequestSessions );
				_marketSubscriptions.erase( contractId );
			}
	 	}
		else
			DBG( "Could not find market data subscription('{}') sessionId='{}'"sv, contractId, sessionId );
		return MarketDataTicks( contractId );
	}*/

	TickerId WebSendGateway::RequestFind( const ClientKey& key )const noexcept
	{
		auto values = _requestSession.Find( [&key]( const auto& value ){ return value==key; } );
		return values.size() ? values.begin()->first : 0;
	}
	void WebSendGateway::Push( EResults type, function<void(MessageType&, SessionPK)> set )noexcept
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
				catch( const Exception& e )
				{
					e.Log();
					orphans.emplace_back( sessionId );
				}
			});
		});
		for_each( orphans.begin(), orphans.end(), [this](auto id){ EraseRequestSession( id ); } );
	}
	void WebSendGateway::Push( EResults type, function<void(MessageType&)> set )noexcept
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
				catch( Exception& e )
				{
					e.Log();
					orphans.emplace_back( sessionId );
				}
			});
		});
		for_each( orphans.begin(), orphans.end(), [this](auto id){ EraseRequestSession( id ); } );
	}
	void WebSendGateway::Push( Proto::Results::EResults messageId, TickerId ibReqId )noexcept
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

	void WebSendGateway::Push( MessageType&& msg, SessionPK id )noexcept(false)
	{
		_webSocket.AddOutgoing( move(msg), id );
	}
	void WebSendGateway::Push( vector<MessageType>&& messages, SessionPK id )noexcept
	{
		_webSocket.AddOutgoing( move(messages), id );
	}

	void WebSendGateway::PushTick( const vector<MessageUnion>& messages, ContractPK contractId )noexcept(false)
	{
		unique_lock l{ _marketSubscriptionMutex };
		if( auto p=_marketSubscriptions.find(contractId); p!=_marketSubscriptions.end() )
		{
			if( p->second.empty() )
			{
				_marketSubscriptions.erase( p );
				THROW( Exception("Could not find any market subscriptions. contractId{} empty", contractId) );
			}
			typedef flat_map<SessionPK,flat_set<Proto::Requests::ETickList>>::iterator X;
			for( X pSession = p->second.begin(); pSession != p->second.end(); )
			{
				try
				{
					_webSocket.AddOutgoing( messages, pSession->first );
					++pSession;
				}
				catch( const Exception& )
				{
					DBG( "Removing session='{}' for contract id='{}'"sv, pSession->first, contractId );
					pSession = p->second.erase( pSession );
				}
			}
		}
		else
			THROW( Exception("Could not find any market subscriptions.") );
	}

	void WebSendGateway::PushError( int errorCode, sv errorString, TickerId id )noexcept
	{
		if( id>0 )
		{
			var key = GetClientRequest( id );
			if( key.SessionId )
				Push( IBException{errorString, errorCode}, key );
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
					pThis->Push( IBException{errorString, errorCode}, key );
				}
				catch( const Exception& e )
				{
					lostIds.push_back( tickerId );
					e.Log();
				}
			});
			for_each( lostIds.begin(), lostIds.end(), [&](auto id){_requestSession.erase(id);} );
		}
	}
	void WebSendGateway::PushError( int errorCode, str errorString, const ClientKey& key )noexcept(false)
	{
		auto pError = make_unique<Proto::Results::Error>(); pError->set_request_id(key.ClientId); pError->set_code(errorCode); pError->set_message(errorString);
		MessageUnion msg; msg.set_allocated_error( pError.release() );
		Push( move(msg), key.SessionId );
	}

	void WebSendGateway::AddOrderSubscription( OrderId orderId, SessionPK sessionId )noexcept
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

	void WebSendGateway::AddMarketDataSubscription( SessionPK sessionId, ContractPK contractId, const flat_set<Proto::Requests::ETickList>& ticks )noexcept
	{
		unique_lock l{ _marketSubscriptionMutex };
		auto& contractSubscriptions = _marketSubscriptions.try_emplace( contractId ).first->second;
		contractSubscriptions[sessionId] = ticks;
	}

	void WebSendGateway::ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, ReqId reqId )noexcept
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
			Push( {msg,*pComplete}, clientKey.SessionId );
		else
			Push( move(msg), clientKey.SessionId );
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
				//_accountMessages[accountNumber][key]=msg;
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
		// else
		// {
		// 	_accountMessages.erase( accountNumber );
		// 	unique_lock l2{_canceledAccountMutex};
		// 	bool found = false;
		// 	for( auto p = _canceledAccounts.begin(); p!=_canceledAccounts.end(); ) //erase old canceled, see if current has been canceled.
		// 	{
		// 		if( p->first==accountNumber )
		// 			found = true;
		// 		p = !found && p->second<Clock::now()-1min ? _canceledAccounts.erase( p ) : next( p );
		// 	}
		// 	if( !found )
		// 	{
		// 		_canceledAccounts.emplace( accountNumber, Clock::now() ); TRACE( "No current listeners for account update '{}', reqAccoun tUpdates"sv, accountNumber );
		// 		_client.reqA ccountUpdates( false, accountNumber );
		// 	}
		// }
	}
	bool WebSendGateway::PortfolioUpdate( const Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept
	{
		return AccountRequest( porfolioUpdate.account_number(), [&porfolioUpdate](MessageType& msg){msg.set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate});} );
	}

	void WebSendGateway::AccountDownloadEnd( sv accountNumber )noexcept
	{
		MessageValue value; value.set_type( Proto::Results::EResults::AccountDownloadEnd ); value.set_string_value( string{accountNumber} );
		AccountRequest( string{accountNumber}, [&value](MessageType& msg)mutable{msg.set_allocated_message( new MessageValue{value});} );
	}

	void WebSendGateway::Push( const Proto::Results::CommissionReport& report )noexcept
	{
		std::shared_lock<std::shared_mutex> l( _executionRequestMutex );
		for( var sessionId : _executionRequests )
		{
			MessageType msg; msg.set_allocated_commission_report( new Proto::Results::CommissionReport{report} );
			Push( move(msg), sessionId );
		}
	}

	void WebSendGateway::Push( Proto::Results::EResults messageId, const ClientKey& key )noexcept(false)
	{
		auto pMessage = new MessageValue(); pMessage->set_int_value( key.ClientId ); pMessage->set_type( messageId );
		MessageType msg; msg.set_allocated_message( pMessage );
		Push( move(msg), key.SessionId );
	}
}