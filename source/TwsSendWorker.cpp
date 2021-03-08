#include "TwsSendWorker.h"
#include "News.h"
#include "WebRequestWorker.h"
#include "../../MarketLibrary/source/types/MyOrder.h"
#include "../../MarketLibrary/source/TickManager.h"
#include "../../Blockly/source/BlocklyLibrary.h"
#include "../jde/blockly/IBlockly.h"
#include "../../Framework/source/io/ProtoUtilities.h"

#define var const auto
#define _tws (*_twsPtr)
#define _web (*_webSendPtr)
#define _cache (*static_pointer_cast<TwsClientCache>(_twsPtr))

#define TwsPtr(sessionId, clientId) auto pTws = static_pointer_cast<TwsClientCache>(_twsPtr); if( !pTws ) return _web.PushError( -5, "Server not connected to TWS.", {{sessionId}, clientId} )

namespace Jde::Markets::TwsWebSocket
{
	TwsSendWorker::TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept:
//		_queue{ make_tuple(0,sp<Proto::Requests::RequestUnion>{}) },
		_webSendPtr{ webSendPtr },
		_twsPtr{ pTwsClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "TwsSendWorker", [&](){Run();} );
	}
	void TwsSendWorker::Push( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionInfo )noexcept
	{
		_queue.Push( {sessionInfo, pData} );
	}
	void TwsSendWorker::Run()noexcept
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.Empty() )
		{
			if( auto p = _queue.WaitAndPop(5s); p )
				HandleRequest( get<1>(*p), get<0>(*p) );
		}
	}

	void TwsSendWorker::HandleRequest( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionKey )noexcept
	{
		ClientPK clientId{0};
		//ClientKey key{key2};
		var& message = *pData;
		//ClientKey key = {sessionId, 0, 0};
		try
		{
			if( message.has_generic_requests() )
				Requests( message.generic_requests(), sessionKey );
			else if( message.has_account_updates() )
				AccountUpdates( message.account_updates(), sessionKey );
			else if( message.has_account_updates_multi() )
				AccountUpdatesMulti( message.account_updates_multi(), sessionKey );
			else if( message.has_market_data_smart() )
				MarketDataSmart( message.market_data_smart(), sessionKey );
			else if( message.has_contract_details() )
				ContractDetails( message.contract_details(), sessionKey );
			else if( message.has_historical_data() )
				HistoricalData( message.historical_data(), sessionKey );
			else if( message.has_place_order() )
				Order( message.place_order(), sessionKey );
			else if( message.has_request_positions() )
				Positions( message.request_positions(), sessionKey );
			else if( message.has_request_executions() )
				Executions( message.request_executions(), sessionKey );
			else if( message.has_news_article_request() )
				News::RequestArticle( message.news_article_request().provider_code(), message.news_article_request().article_id(), {sessionKey, clientId=message.news_article_request().id(), _webSendPtr} );
			else if( message.has_historical_news_request() )
				News::RequestHistorical( message.historical_news_request().contract_id(), message.historical_news_request().provider_codes(), message.historical_news_request().total_results(), message.historical_news_request().start(), message.historical_news_request().end(), {sessionKey, clientId=message.historical_news_request().id(), _webSendPtr} );
			else if( message.has_implied_volatility() )
				CalculateImpliedVolatility( message.implied_volatility().contracts(), {sessionKey, clientId=message.implied_volatility().id()} );
			else if( message.has_implied_price() )
				CalculateImpliedPrice( message.implied_price().contracts(), {sessionKey, clientId=message.implied_price().id()} );
			else
				ERR( "Unknown Message '{}'"sv, message.Value_case() );
		}
		catch( const IBException& e )
		{
			_webSendPtr->Push( e, {sessionKey, clientId} );
		}
		catch( const Exception& e )
		{
			_webSendPtr->Push( e, {sessionKey, clientId} );
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
			TickManager::CalcImpliedVolatility( client.SessionId, client.ClientId, GetContract(r.contract_id()), r.option_price(), r.underlying_price(), [this](auto a, auto b){ _webSendPtr->PushTick(a,b);} );
	}

	void TwsSendWorker::CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept
	{
		for( var& r : requests )
			TickManager::CalculateOptionPrice( client.SessionId, client.ClientId, GetContract(r.contract_id()), r.volatility(), r.underlying_price(), [this](auto a, auto b){ _webSendPtr->PushTick(a,b);} );
	}

	void TwsSendWorker::ContractDetails( const Proto::Requests::RequestContractDetails& r, const SessionKey& key )noexcept
	{
		var clientRequestId = r.id();
		TwsPtr( key.SessionId, clientRequestId );
		flat_set<TickerId> requestIds;
		for( int i=0; i<r.contracts_size(); ++i )
			requestIds.emplace( pTws->RequestId() );
		_web.AddMultiRequest( requestIds, {{key.SessionId}, clientRequestId} );
		int i=0;
		for( var reqId : requestIds )
		{
			const Contract contract{ r.contracts(i++) };//
			TRACE( "({}.{})ContractDetails( contract='{}' )"sv, key.SessionId, reqId, contract.Symbol );

			if( !contract.Id && contract.SecType==SecurityType::Option )
				((TwsClientCache&)*pTws).ReqContractDetails( reqId, TwsClientCache::ToContract(contract.Symbol, contract.Expiration, contract.Right) );
			else
				((TwsClientCache&)*pTws).ReqContractDetails( reqId, *contract.ToTws() );
		}
	}

	void TwsSendWorker::HistoricalData( const Proto::Requests::RequestHistoricalData& r, const SessionKey& session )noexcept(false)
	{
		_tws.ReqHistoricalData( _web.AddRequestSession({{session.SessionId},r.id()}), Contract{r.contract()}, Chrono::DaysSinceEpoch(Clock::from_time_t(r.date())), r.days(), r.bar_size(), r.display(), r.use_rth() );
	}

	void TwsSendWorker::Executions( const Proto::Requests::RequestExecutions& r, const SessionKey& session )noexcept
	{
		_web.AddExecutionRequest( session.SessionId );

		DateTime time{ r.time() };
		var timeString = format( "{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", time.Year(), time.Month(), time.Day(), time.Hour(), time.Minute(), time.Second() );
		ExecutionFilter filter; filter.m_clientId=r.client_id(); filter.m_acctCode=r.account_number(); filter.m_time=timeString; filter.m_symbol=r.symbol(); filter.m_secType=r.security_type(); filter.m_exchange=r.exchange(); filter.m_side=r.side();
		_tws.reqExecutions( _web.AddRequestSession({{session.SessionId},r.id()}), filter );
	}

	void TwsSendWorker::Order( const Proto::Requests::PlaceOrder& r, const SessionKey& session )noexcept
	{
		var reqId = _web.AddRequestSession( {{session.SessionId},r.id()}, r.order().id() );
		const Jde::Markets::Contract contract{ r.contract() };
		var pIbContract = contract.ToTws();
		const MyOrder order{ reqId, r.order() };
		DBG( "({})receiveOrder( '{}', contract='{}' {}x{} )"sv, reqId, session.SessionId, pIbContract->symbol, order.lmtPrice, order.totalQuantity );
		_tws.placeOrder( *pIbContract, order );
		if( r.block_id().size() )
		{
			try
			{
				auto pp = up<sp<Markets::MBlockly::IBlockly>>( Blockly::CreateAllocatedExecutor(r.block_id(), order.orderId, contract.Id) );
				auto p = *pp;
				p->Run();
			}
			catch( const Exception& e )
			{
				e.Log();
				_webSendPtr->Push( e, {{session.SessionId}, r.id()} );
			}
			//todo allow cancel
/*			std::thread{ [ pBlockly=*p ]()
			{
				pBlockly->Run();
				while( pBlockly->Running() )
					std::this_thread::sleep_for( 5s );
			}}.detach();
*/
		}
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

	void TwsSendWorker::Positions( const Proto::Requests::RequestPositions& r, const SessionKey& session )noexcept
	{
		_tws.reqPositionsMulti( _web.AddRequestSession({{session.SessionId},r.id()}), r.account_number(), r.model_code() );
	}

	void TwsSendWorker::Requests( const Proto::Requests::GenericRequests& r, const SessionKey& session )noexcept
	{
		if( r.type()==ERequests::Positions )
		{
			_web.AddRequestSessions( session.SessionId, {EResults::PositionEnd, EResults::PositionData} );
			//if( _web.Add(ERequests::Positions) ) TODO test how this works with multiple calls.  and if canceled appropriately.
				//_tws.reqPositions();
			ERR( "reqPositions not implemented."sv );
			_web.PushError( -1, "reqPositions not implemented.", {{session.SessionId}, r.id()} );
		}
		else if( r.type()==ERequests::ManagedAccounts )
		{
			_web.AddRequestSessions( session.SessionId, {EResults::ManagedAccounts} );
			_tws.reqManagedAccts();
		}
		else if( r.type()==ERequests::RequestOpenOrders )
		{
			_web.AddRequestSessions( session.SessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_tws.reqOpenOrders();
		}
		else if( r.type()==ERequests::RequestAllOpenOrders )
		{
			_web.AddRequestSessions( session.SessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );
			_tws.reqAllOpenOrders();//not a subscription.
		}
		else if( r.type()==ERequests::CancelMarketData )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var contractId = r.ids( i );
				TickManager::CancelProto( session.SessionId , 0, contractId );
			}
		}
		else if( r.type()==ERequests::CancelPositionsMulti )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var ibId = _web.RequestFind( {{session.SessionId}, (ClientPK)r.ids(i)} );
				if( ibId )
				{
					_tws.cancelPositionsMulti( ibId );
					_web.RequestErase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'"sv, session.SessionId, r.ids(i) );
			}
		}
		else if( r.type()==ERequests::CancelOrder )
		{
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var orderId = r.ids(i);
				_web.AddOrderSubscription( orderId, session.SessionId );
				_tws.cancelOrder( orderId );
			}
		}
		else if( r.type()==ERequests::RequestOptionParams )
			RequestOptionParams( r.ids(), {session.SessionId, r.id()} );
		else if( r.type()==ERequests::ReqNewsProviders )
		{
			_web.AddRequestSessions( session.SessionId, {EResults::NewsProviders} );
			_cache.RequestNewsProviders();
		}
		else
			WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, r.type(), session.SessionId );
	}

	void TwsSendWorker::AccountUpdates( const Proto::Requests::RequestAccountUpdates& accountUpdates, const SessionKey& session )noexcept
	{
		var& account = accountUpdates.account_number();
		var subscribe = accountUpdates.subscribe();
		if( subscribe )
			_web.AddAccountSubscription( account, session.SessionId );
		else
			_web.CancelAccountSubscription( account, session.SessionId );
	}

	void TwsSendWorker::AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& r, const SessionKey& session )noexcept
	{
		var reqId =  _web.AddRequestSession( {{session.SessionId},r.id()} );
		var& account = r.account_number();

		DBG( "reqAccountUpdatesMulti( reqId='{}' sessionId='{}', account='{}', clientId='{}' )"sv, reqId, session.SessionId, account, r.id() );
		_tws.reqAccountUpdatesMulti( reqId, account, r.model_code(), r.ledger_and_nlv() );
	}
	void TwsSendWorker::MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& r, const SessionKey& session )noexcept
	{
		flat_set<Proto::Requests::ETickList> ticks;
		std::for_each( r.tick_list().begin(), r.tick_list().end(), [&ticks]( auto item ){ ticks.emplace((Proto::Requests::ETickList)item); } );
		_webSendPtr->AddMarketDataSubscription( session.SessionId, r.contract_id(), ticks );
		TickManager::Subscribe( session.SessionId, 0, r.contract_id(), ticks, r.snapshot(), [this](var& a, auto b){ _webSendPtr->PushTick(a,b);} );
	}

	void TwsSendWorker::RequestOptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )noexcept
	{
		try
		{
			for( ClientPK underlyingId : underlyingIds )
			{
				if( !underlyingId )
					THROW( Exception("Requested underlyingId=='{}'.", underlyingId) );
				_cache.ReqSecDefOptParams( _web.AddRequestSession({key}), underlyingId, GetContract(underlyingId).localSymbol );
			}
		}
		catch( const Exception& e )
		{
			_web.Push( e, {key} );
		}
	}
}
