#include "WebSendGateway.h"
#include "WebSocket.h"

#define var const auto
#define _client dynamic_cast<TwsClientCache&>(TwsClientSync::Instance())
namespace Jde::Markets::TwsWebSocket
{
	WebSendGateway::WebSendGateway( WebSocket& webSocketParent, sp<TwsClientSync> pClientSync )noexcept:
		_queue{ make_tuple(0, MessageTypePtr{}) },
		_webSocket{ webSocketParent },
		_pClientSync{ pClientSync }
	{
		DBG0( "WebSendGateway::WebSendGateway"sv );
	}

	void WebSendGateway::EraseSession( SessionId id )noexcept
	{
		DBG( "Removing session '{}'"sv, id );
//		_sessions.erase( id );
		std::function<void(const EResults&, UnorderedSet<SessionId>& )> func = [id]( const EResults& messageId, UnorderedSet<SessionId>& sessions )
		{
			//sessions.EraseIf( [&](const SessionId& id){return !_sessions.Find(id);} );//if lost others.
			sessions.erase( id );
			if( messageId==EResults::PositionData )
			{
				sessions.IfEmpty( [&]()
				{
					_client.cancelPositions();
					sessions.erase( ERequests::Positions );
				});
			}
		};
		_requestSessions.ForEach( func );
		std::unique_lock<std::shared_mutex> l( _accountSubscriptionMutex );
		for( auto pAccountSessionIds = _accountSubscriptions.begin(); pAccountSessionIds!=_accountSubscriptions.end();  )
		{
			var& accountNumber = pAccountSessionIds->first;
			auto& sessionIds =   pAccountSessionIds->second;
			if( sessionIds.erase(id) && sessionIds.size()==0 )
			{
				_client.reqAccountUpdates( false, accountNumber );
				pAccountSessionIds = _accountSubscriptions.erase( pAccountSessionIds );
			}
			else
				++pAccountSessionIds;
		}

		auto pLock = make_unique<std::unique_lock<mutex>>( _marketSubscriptionsMutex );
		for( auto pContractSessions = _marketSessionSubscriptions.begin(); pContractSessions!=_marketSessionSubscriptions.end(); )
		{
			if( auto pDoomedTicks = pContractSessions->second.find( id ); pDoomedTicks!=pContractSessions->second.end() )
			{
				var contractId = pContractSessions->first;
				if( var [reqId,ticks] = RemoveMarketDataSubscription(contractId, id, true); reqId )//~~~
				{
					if( ticks.size() )
						Try( [&,reqId2=reqId, &ticks=ticks]()
						{
							var pDetails = _pClientSync->ReqContractDetails( contractId ).get();
							if( pDetails->size()!=1 )
								THROW( Exception("contractId={} returned {} records"sv, contractId, pDetails->size()) );
							_client.reqMktData( reqId2, pDetails->front().contract, StringUtilities::AddCommas(ticks), false/*snapshot*/, false/*regulatory*/, {} );
						} );
					else
						_client.cancelMktData( reqId );
				}
				pContractSessions = _marketSessionSubscriptions.upper_bound( contractId );
			}
			else
				++pContractSessions;
		}
	}

	void WebSendGateway::AddMultiRequest( const flat_set<TickerId>& ids, const ClientKey& key )
	{
		{
			unique_lock l{_multiRequestMutex};
			_multiRequests.emplace( key.ClientId, ids );
		}
		for_each( ids.begin(), ids.end(), [this, &key](auto id){ _requestSession.emplace(id, key); } );
	}

	void WebSendGateway::AddRequestSessions( SessionId id, const vector<EResults>& webSendMessages )noexcept//todo arg forwarding.
	{
		auto afterInsert = [id](UnorderedSet<SessionId>& values){values.emplace(id);};
		for( var sendMessage : webSendMessages )
			_requestSessions.Insert( afterInsert, sendMessage, sp<UnorderedSet<SessionId>>{new UnorderedSet<SessionId>{}} );
	}
	bool WebSendGateway::AddAccountSubscription( const string& account, SessionId sessionId )noexcept
	{
		DBG( "Account('{}') subscription for sessionId='{}'"sv, account, sessionId );
		unique_lock l{ _accountSubscriptionMutex };
		auto [pValue, inserted] = _accountSubscriptions.try_emplace( account );
		pValue->second.emplace( sessionId );
		return inserted;
	}
	bool WebSendGateway::CancelAccountSubscription( const string& account, SessionId sessionId )noexcept
	{
		DBG( "Account('{}') unsubscribe for sessionId='{}'"sv, account, sessionId );
		unique_lock l{ _accountSubscriptionMutex };
		auto pValue = _accountSubscriptions.find( account );
		auto cancel = pValue==_accountSubscriptions.end();
		if( !cancel )
		{
			pValue->second.erase( sessionId );
			cancel = !pValue->second.size();
			if( cancel )
				_accountSubscriptions.erase( pValue );
		}
		else
			DBG( "Could not find account('{}') subscription for sessionId='{}'"sv, account, sessionId );
		return cancel;
	}
	tuple<TickerId, flat_set<Proto::Requests::ETickList>> WebSendGateway::MarketDataTicks( ContractPK contractId )noexcept
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
	tuple<TickerId,flat_set<Proto::Requests::ETickList>> WebSendGateway::AddMarketDataSubscription( ContractPK contractId, flat_set<Proto::Requests::ETickList>&& ticks, SessionId sessionId )noexcept
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
	tuple<TickerId,flat_set<Proto::Requests::ETickList>> WebSendGateway::RemoveMarketDataSubscription( ContractPK contractId, SessionId sessionId, bool haveLock )noexcept
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
	}

	TickerId WebSendGateway::RequestFind( const ClientKey& key )const noexcept
	{
		auto values = _requestSession.Find( [&key]( const auto& value ){ return value==key; } );
		return values.size() ? values.begin()->first : 0;
	}
	void WebSendGateway::Push( EResults type, function<void(MessageType&)> set )noexcept
	{
		_requestSessions.Where( type, [&]( const auto& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId )
			{
				auto pMessageUnion = make_shared<MessageType>();
				set( *pMessageUnion ); ASSERT( pMessageUnion->Value_case()!=MessageType::ValueCase::VALUE_NOT_SET );
				Push( pMessageUnion, sessionId );
			});
		});
	}
	void WebSendGateway::Push( Proto::Results::EResults messageId, TickerId ibReqId )noexcept
	{
		var [sessionId,clientId] = GetClientRequest( ibReqId );
		if( sessionId )
		{
			auto pMessage = new Proto::Results::MessageValue(); pMessage->set_int_value( clientId ); pMessage->set_type( messageId );
			auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_message( pMessage );
			Push( pUnion, sessionId );
		}
		else
			DBG( "Could not find session for messageId:  '{}' req:  '{}'."sv, messageId, ibReqId );
	}

	void WebSendGateway::Push( MessageTypePtr pUnion, SessionId id )noexcept
	{
		_webSocket.AddOutgoing( pUnion, id );
	}
	void WebSendGateway::Push( const vector<MessageTypePtr>& messages, SessionId id )noexcept
	{
		_webSocket.AddOutgoing( messages, id );
	}

	void WebSendGateway::PushError( int errorCode, string_view errorString, TickerId id )noexcept
	{
		if( id>0 )
		{
			var key = GetClientRequest( id );
			if( key.SessionPK )
				Push( IBException{errorString, errorCode}, key );
			else
				DBG( "Could not find session for error req:  '{}'."sv, id );
		}
		else if( errorCode==504 )
		{
			_requestSession.ForEach( [errorCode, &errorString, pThis=shared_from_this()](auto /*key*/, const auto& key)
			{
				pThis->Push( IBException{errorString, errorCode}, key );
			});
		}
	}
	void WebSendGateway::PushError( int errorCode, const string& errorString, const ClientKey& key )noexcept
	{
		auto pError = new Proto::Results::Error(); pError->set_request_id(key.ClientId); pError->set_code(errorCode); pError->set_message(errorString);
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_error( pError );
		Push( pMessage, key.SessionPK );
	}

	void WebSendGateway::AddOrderSubscription( OrderId orderId, SessionId sessionId )noexcept
	{
		unique_lock l{ _orderSubscriptionMutex };

		auto& sessions = _orderSubscriptions.try_emplace( orderId ).first->second;
		sessions.emplace( sessionId );
	}


/*	void WebSendGateway::PushError( SessionId sessionId, ClientRequestId clientId, int errorCode, const std::string& errorString )noexcept
	{
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_error( pError );
		Push( sessionId, pMessage );
	}*/

/*	void WebSendGateway::PushAllocated( Proto::Results::ContractDetails* pDetails, TickerId reqId )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, ClientKey{} );
		if( sessionId )
		{
			pDetails->set_request_id( clientReqId );
			auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_contract_details( pDetails );
			Push( sessionId, pMessageUnion );
		}
		else
			DBG( "Could not find session for req:  '{}'."sv, reqId );
	}
*/
	bool WebSendGateway::Push( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( id, ClientKey{} );
		if( sessionId )
		{
			auto pUnion = make_shared<MessageType>();
			set( *pUnion, clientReqId );
			Push( pUnion, sessionId );
		}
		return sessionId;
	}

	void WebSendGateway::PushMarketData( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept
	{
		bool cancel = false; ContractPK contractId = 0;
		{
			unique_lock l{ _marketSubscriptionsMutex };
			if( var pContract = _marketTicketContractMap.find(id); pContract!=_marketTicketContractMap.end() )
			{
				contractId = pContract->second;
				if( var pSessions = _marketSessionSubscriptions.find(contractId); pSessions!=_marketSessionSubscriptions.end() )
				{
					for( var& sessionTicks : pSessions->second )
					{
						auto pUnion = make_shared<MessageType>();
						set( *pUnion, contractId );
						Push( pUnion, sessionTicks.first );
					}
				}
				else
				{
					ERR( "({})Canceling no sessions found for contract '{}' found."sv, id, contractId );
					_marketTicketContractMap.erase( pContract );
					cancel = true;
				}
			}
			else
			{
				ERR( "({})Canceling for {}, no contracts found."sv, id, contractId );//TODO:  keep track of recently canceled tickers.
				cancel = true;
			}
		}
		if( cancel )
			_pClientSync->cancelMktData( id );
	}

	void WebSendGateway::ContractDetails( unique_ptr<Proto::Results::ContractDetailsResult> pDetails, ReqId reqId )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, ClientKey{} );
		if( !sessionId )
		{
			DBG( "Could not find session for ContractDetailsEnd req:  '{}'."sv, reqId );
			return;
		}
		bool multi=false;//web call requested multiple contract details
		sp<MessageType> pComplete;
		{
			unique_lock l{_multiRequestMutex};
			auto pMultiRequests = _multiRequests.find( clientReqId );
			multi = pMultiRequests!=_multiRequests.end();
			if( multi )
			{
				auto& reqIds = pMultiRequests->second;
				reqIds.erase( reqId );
				if( reqIds.size()==0 )
				{
					_multiRequests.erase( pMultiRequests );
					auto pMessage = new Proto::Results::MessageValue(); pMessage->set_type( Proto::Results::EResults::MultiEnd ); pMessage->set_int_value( clientReqId );
					pComplete = make_shared<MessageType>();  pComplete->set_allocated_message( pMessage );
				}
			}
		}
		pDetails->set_request_id( clientReqId );
		auto pUnion = make_shared<MessageType>();
		pUnion->set_allocated_contract_details( pDetails.release() );
		if( pComplete )
			Push( {pUnion,pComplete}, sessionId );
		else
			Push( pUnion, sessionId );
		if( pComplete || !multi )
			_requestSession.erase( reqId );
	}

/*	void WebSendGateway::PushAllocated( unique_ptr<Proto::Results::AccountUpdateMulti> pMessage )noexcept
	{
		var requestId = pMessage->request_id();
		var [sessionId, clientReqId] = _requestSession.Find( requestId, {} );
		if( sessionId )
		{
			pMessage->set_request_id( clientReqId );
			auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update_multi( pMessage.release() );
			Push( sessionId, pUnion );
		}
		else
		{
			DBG( "({})AccountUpdateMulti not found"sv, requestId );
			_client.cancelPositionsMulti( requestId );
		}
	}
*/
	void WebSendGateway::AccountRequest( const string& accountNumber, function<void(MessageType&)> setMessage )noexcept
	{
		std::shared_lock<std::shared_mutex> l( _accountSubscriptionMutex );
		var pAccountNumberSessions = _accountSubscriptions.find( accountNumber );
		if( pAccountNumberSessions==_accountSubscriptions.end() )
		{
			WARN( "No listeners for portfolio update '{}'"sv, accountNumber );
			_client.reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>();
				setMessage( *pUnion );
				Push( pUnion, sessionId );
			}
		}
	}
	void WebSendGateway::Push( const Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept
	{
		AccountRequest( porfolioUpdate.account_number(), [&porfolioUpdate](MessageType& msg){msg.set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate});} );
	}
	void WebSendGateway::PushAccountDownloadEnd( const string& accountNumber )noexcept
	{
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::AccountDownloadEnd ); pValue->set_string_value( accountNumber );
		AccountRequest( accountNumber, [pValue](MessageType& msg){msg.set_allocated_message(pValue);} );
	}
	void WebSendGateway::Push( const Proto::Results::AccountUpdate& accountUpdate )noexcept
	{
		var accountNumber = accountUpdate.account();
		std::shared_lock<std::shared_mutex> l( _accountSubscriptionMutex );
		var pAccountNumberSessions = _accountSubscriptions.find( accountNumber );
		if( pAccountNumberSessions==_accountSubscriptions.end() )
		{
			WARN( "No current listeners for account update '{}', reqAccountUpdates"sv, accountNumber );
			_client.reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update( new Proto::Results::AccountUpdate{accountUpdate} );
				Push( pUnion, sessionId );
			}
		}
	}

	void WebSendGateway::Push( const Proto::Results::CommissionReport& report )noexcept
	{
		std::shared_lock<std::shared_mutex> l( _executionRequestMutex );
		for( var sessionId : _executionRequests )
		{
			auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_commission_report( new Proto::Results::CommissionReport{report} );
			Push( pUnion, sessionId );
		}
	}

	void WebSendGateway::Push( Proto::Results::EResults messageId, const ClientKey& key )noexcept
	{
		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_int_value( key.ClientId ); pMessage->set_type( messageId );
		auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_message( pMessage );
		Push( pUnion, key.SessionPK );
	}
}