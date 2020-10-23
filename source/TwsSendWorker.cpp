#include "TwsSendWorker.h"
#include "News.h"
#include "WebRequestWorker.h"
#include "../../MarketLibrary/source/types/MyOrder.h"
#include "../../Framework/source/io/ProtoUtilities.h"

#define var const auto
#define _tws (*_twsPtr)
#define _web (*_webSendPtr)
#define _cache (*static_pointer_cast<TwsClientCache>(_twsPtr))
namespace Jde::Markets::TwsWebSocket
{
	TwsSendWorker::TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept:
		_queue{ make_tuple(0,sp<Proto::Requests::RequestUnion>{}) },
		_webSendPtr{ webSendPtr },
		_twsPtr{ pTwsClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebRequestWorker", [&](){Run();} );
	}
	void TwsSendWorker::Push( sp<Proto::Requests::RequestUnion> pData, SessionId sessionId )noexcept
	{
		_queue.Push( make_tuple(sessionId, pData) );
	}
	void TwsSendWorker::Run()noexcept
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.Empty() )
		{
			var [sessionId, pData] = _queue.WaitAndPop( 5s );
			if( pData )
				HandleRequest( pData, sessionId );
		}
	}

	void TwsSendWorker::HandleRequest( sp<Proto::Requests::RequestUnion> pData, SessionId sessionId )noexcept
	{
		var message = *pData;
		ClientKey key = {sessionId, 0};
		try
		{
			if( message.has_generic_requests() )
				Requests( message.generic_requests(), sessionId );
			else if( message.has_account_updates() )
				AccountUpdates( message.account_updates(), sessionId );
			else if( message.has_account_updates_multi() )
				AccountUpdatesMulti( message.account_updates_multi(), sessionId );
			else if( message.has_market_data_smart() )
				MarketDataSmart( message.market_data_smart(), sessionId );
			else if( message.has_contract_details() )
				ContractDetails( message.contract_details(), sessionId );
			else if( message.has_historical_data() )
				HistoricalData( message.historical_data(), sessionId );
			else if( message.has_place_order() )
				Order( message.place_order(), sessionId );
			else if( message.has_request_positions() )
				Positions( message.request_positions(), sessionId );
			else if( message.has_request_executions() )
				Executions( message.request_executions(), sessionId );
			else if( message.has_news_article_request() )
				News::RequestArticle( message.news_article_request().provider_code(), message.news_article_request().article_id(), {key={sessionId, message.news_article_request().id()}, _webSendPtr} );
			else if( message.has_historical_news_request() )
				News::RequestHistorical( message.historical_news_request().contract_id(), message.historical_news_request().provider_codes(), message.historical_news_request().total_results(), message.historical_news_request().start(), message.historical_news_request().end(), {key={sessionId, message.historical_news_request().id()}, _webSendPtr} );
			else if( message.has_implied_volatility() )
				CalculateImpliedVolatility( message.implied_volatility().contracts(), key={sessionId, message.implied_volatility().id()} );
			else if( message.has_implied_price() )
				CalculateImpliedPrice( message.implied_price().contracts(), key={sessionId, message.implied_price().id()} );
			else
				ERR( "Unknown Message '{}'"sv, message.Value_case() );
		}
		catch( const IBException& e )
		{
			_webSendPtr->Push( e, key );
		}
		catch( const Exception& e )
		{
			_webSendPtr->Push( e, key );
		}
	}

	::Contract TwsSendWorker::GetContract( ContractPK contractId )noexcept(false)
	{
		var pDetails = _tws.ReqContractDetails( contractId ).get();
		if( pDetails->size()!=1 )
			THROW( Exception("contractId={} returned {} records"sv, contractId, pDetails->size()) );
		return pDetails->front().contract;
	}

	void TwsSendWorker::CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )noexcept(false)
	{
		for( var& r : requests )
			_tws.calculateImpliedVolatility( _web.AddRequestSession(client), GetContract(r.contract_id()), r.option_price(), r.underlying_price(), {} );
	}

	void TwsSendWorker::CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept
	{
		for( var& r : requests )
			_tws.calculateOptionPrice( _web.AddRequestSession(client), GetContract(r.contract_id()), r.volatility(), r.underlying_price(), {} );
	}

	void TwsSendWorker::ContractDetails( const Proto::Requests::RequestContractDetails& r, SessionId sessionId )noexcept
	{
		var clientRequestId = r.id();
		flat_set<TickerId> requestIds;
		for( int i=0; i<r.contracts_size(); ++i )
			requestIds.emplace( _tws.RequestId() );
		_web.AddMultiRequest( requestIds, {sessionId, clientRequestId} );
		int i=0;
		for( var reqId : requestIds )
		{
			const Contract contract{ r.contracts(i++) };//
			DBG( "reqContractDetails( reqId='{}' sessionId='{}', contract='{}' )"sv, reqId, sessionId, contract.Symbol );

			if( contract.SecType==SecurityType::Option )
				((TwsClientCache&)_tws).ReqContractDetails( reqId, TwsClientCache::ToContract(contract.Symbol, contract.Expiration, contract.Right) );
			else
				((TwsClientCache&)_tws).ReqContractDetails( reqId, *contract.ToTws() );
		}
	}

	void TwsSendWorker::HistoricalData( const Proto::Requests::RequestHistoricalData& r, SessionId sessionId )noexcept(false)
	{
		_tws.ReqHistoricalData( _web.AddRequestSession({sessionId,r.id()}), Contract{r.contract()}, Chrono::DaysSinceEpoch(Clock::from_time_t(r.date())), r.days(), r.bar_size(), r.display(), r.use_rth() );
	}

	void TwsSendWorker::Executions( const Proto::Requests::RequestExecutions& r, SessionId sessionId )noexcept
	{
		_web.AddExecutionRequest( sessionId );

		DateTime time{ r.time() };
		var timeString = format( "{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", time.Year(), time.Month(), time.Day(), time.Hour(), time.Minute(), time.Second() );
		ExecutionFilter filter; filter.m_clientId=r.client_id(); filter.m_acctCode=r.account_number(); filter.m_time=timeString; filter.m_symbol=r.symbol(); filter.m_secType=r.security_type(); filter.m_exchange=r.exchange(); filter.m_side=r.side();
		_tws.reqExecutions( _web.AddRequestSession({sessionId,r.id()}), filter );
	}

	void TwsSendWorker::Order( const Proto::Requests::PlaceOrder& r, SessionId sessionId )noexcept
	{
		var reqId = _web.AddRequestSession( {sessionId,r.id()}, r.order().id() );

		var pIbContract = Jde::Markets::Contract{ r.contract() }.ToTws();
		const MyOrder order{ reqId, r.order() };
		DBG( "({})receiveOrder( '{}', contract='{}' {}x{} )"sv, reqId, sessionId, pIbContract->symbol, order.lmtPrice, order.totalQuantity );
		_tws.placeOrder( *pIbContract, order );
/*		if( !order.whatIf && r.stop()>0 )
		{
			var parentId = _tws.RequestId();
			_requestSession.emplace( parentId, make_tuple(sessionId, r.id()) );
			Jde::Markets::MyOrder parent{ parentId, r.order() };
			parent.IsBuy( !order.IsBuy() );
			parent.OrderType( r::EOrderType::StopLimit );
			parent.totalQuantity = order.totalQuantity;
			parent.auxPrice = r.stop();
			parent.lmtPrice = r.stop_limit();
			parent.parentId = reqId;
			_tws.placeOrder( *pIbContract, parent );
		}*/
	}

	void TwsSendWorker::Positions( const Proto::Requests::RequestPositions& r, SessionId sessionId )noexcept
	{
		_tws.reqPositionsMulti( _web.AddRequestSession({sessionId,r.id()}), r.account_number(), r.model_code() );
	}

	void TwsSendWorker::Requests( const Proto::Requests::GenericRequests& r, SessionId sessionId )noexcept
	{
		if( r.type()==ERequests::Positions )
		{
			_web.AddRequestSessions( sessionId, {EResults::PositionEnd, EResults::PositionData} );
			//if( _web.Add(ERequests::Positions) ) TODO test how this works with multiple calls.  and if canceled appropriately.
				//_tws.reqPositions();
			ERR0( "reqPositions not implemented."sv );
			_web.PushError( -1, "reqPositions not implemented.", {sessionId, r.id()} );
		}
		else if( r.type()==ERequests::ManagedAccounts )
		{
			_web.AddRequestSessions( sessionId, {EResults::ManagedAccounts} );
			_tws.reqManagedAccts();
		}
		else if( r.type()==ERequests::RequestOpenOrders )
		{
			_web.AddRequestSessions( sessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_tws.reqOpenOrders();
		}
		else if( r.type()==ERequests::RequestAllOpenOrders )
		{
			_web.AddRequestSessions( sessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_tws.reqAllOpenOrders();
		}
		else if( r.type()==ERequests::CancelMarketData )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var contractId = r.ids( i );
				unique_lock<std::mutex> l{_mktDataMutex};
				if( var [reqId,ticks] = _web.RemoveMarketDataSubscription(contractId, sessionId); reqId )
				{
					if( ticks.size() )
						Try( [&,reqId2=reqId, &ticks=ticks](){ _tws.reqMktData( reqId2, GetContract(contractId), StringUtilities::AddCommas(ticks), false/*snapshot*/, false/*regulatory*/, {} ); } );
					else
						_tws.cancelMktData( reqId );
				}
			}
		}
		else if( r.type()==ERequests::CancelPositionsMulti )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var ibId = _web.RequestFind( {sessionId, (ClientRequestId)r.ids(i)} );
				if( ibId )
				{
					_tws.cancelPositionsMulti( ibId );
					_web.RequestErase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'"sv, sessionId, r.ids(i) );
			}
		}
		else if( r.type()==ERequests::CancelOrder )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				OrderId orderId = r.ids(i);
				_web.AddOrderSubscription( orderId, sessionId );
				_tws.cancelOrder( orderId );
			}
		}
		else if( r.type()==ERequests::RequestOptionParams )
			RequestOptionParams( r.ids(), {sessionId, r.id()} );
		else if( r.type()==ERequests::ReqNewsProviders )
		{
			_web.AddRequestSessions( sessionId, {EResults::NewsProviders} );
			_cache.RequestNewsProviders();
		}
		else
			WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, r.type(), sessionId );
	}

	void TwsSendWorker::AccountUpdates( const Proto::Requests::RequestAccountUpdates& accountUpdates, SessionId sessionId )noexcept
	{
		var& account = accountUpdates.account_number();
		var subscribe = accountUpdates.subscribe();
		if( subscribe )
		{
			if( _web.AddAccountSubscription(account, sessionId) )
			{
				DBG( "Subscribe to account updates '{}'"sv, account );
				_tws.reqAccountUpdates( true, account );
			}
		}
		else if( _web.CancelAccountSubscription(account, sessionId) )
			_tws.reqAccountUpdates( false, account );
	}

	void TwsSendWorker::AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& r, SessionId sessionId )noexcept
	{
		var reqId =  _web.AddRequestSession( {sessionId,r.id()} );
		var& account = r.account_number();

		DBG( "reqAccountUpdatesMulti( reqId='{}' sessionId='{}', account='{}', clientId='{}' )"sv, reqId, sessionId, account, r.id() );
		_tws.reqAccountUpdatesMulti( reqId, account, r.model_code(), r.ledger_and_nlv() );
	}
	void TwsSendWorker::MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& r, SessionId sessionId )noexcept
	{
		var contractId = r.contract_id();
		::Contract contract; contract.conId = contractId; contract.exchange = "SMART";
		flat_set<Proto::Requests::ETickList> ticks;
		for_each( r.tick_list().begin(), r.tick_list().end(), [&ticks]( auto item ){ ticks.emplace((Proto::Requests::ETickList)item); } );

		if( var [reqId, newTicks] = _web.AddMarketDataSubscription(contractId, move(ticks), sessionId); reqId )
			_tws.reqMktData( reqId, contract, StringUtilities::AddCommas(newTicks), r.snapshot(), false, {} );
	}

	void TwsSendWorker::RequestOptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept
	{
		try
		{
			for( ClientRequestId underlyingId : underlyingIds )
			{
				if( !underlyingId )
					THROW( Exception("Requested underlyingId=='{}'.", underlyingId) );
				_cache.ReqSecDefOptParams( _web.AddRequestSession(key), underlyingId, GetContract(underlyingId).localSymbol );
			}
		}
		catch( const Exception& e )
		{
			_web.Push( e, key );
		}
	}
}
