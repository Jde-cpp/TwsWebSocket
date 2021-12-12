#include "TwsSendWorker.h"
#include "requests/News.h"
#include "WebRequestWorker.h"
#include <jde/markets/types/MyOrder.h>
#include "../../MarketLibrary/source/TickManager.h"
#include <jde/blockly/BlocklyLibrary.h>
#include <jde/blockly/IBlockly.h>
#include "../../Framework/source/io/ProtoUtilities.h"

#define var const auto
#define _tws (*_twsPtr)
#define _web (*_webSendPtr)
#define _cache (*static_pointer_cast<TwsClientCache>(_twsPtr))

#define TwsPtr(sessionId, clientId) auto pTws = static_pointer_cast<TwsClientCache>(_twsPtr); if( !pTws ) return _web.PushError( -5, "Server not connected to TWS.", {{sessionId}, clientId} )

namespace Jde::Markets::TwsWebSocket
{
	static const LogTag& _logLevel = Logging::TagLevel( "app.webRequests" );

	TwsSendWorker::TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept:
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
		auto& m = *pData;
		try
		{
			if( m.has_generic_requests() )
				Requests( m.generic_requests(), sessionKey );
			else if( m.has_generic_request() )
				Request( m.generic_request(), sessionKey );
			else if( m.has_account_updates() )
				AccountUpdates( m.account_updates(), sessionKey );
			else if( m.has_account_updates_multi() )
				AccountUpdatesMulti( m.account_updates_multi(), sessionKey );
			else if( m.has_market_data_smart() )
				MarketDataSmart( m.market_data_smart(), sessionKey );
			else if( m.has_contract_details() )
				ContractDetails( m.contract_details(), sessionKey );
			else if( m.has_historical_data() )
				HistoricalData( move(*m.mutable_historical_data()), sessionKey );
			else if( m.has_place_order() )
				Order( m.place_order(), sessionKey );
			else if( m.has_request_positions() )
				Positions( m.request_positions(), sessionKey );
			else if( m.has_request_executions() )
				Executions( m.request_executions(), sessionKey );
			else if( m.has_news_article_request() )
				News::RequestArticle( m.news_article_request().provider_code(), m.news_article_request().article_id(), {sessionKey, clientId=m.news_article_request().id(), _webSendPtr} );
			else if( m.has_historical_news_request() )
				News::RequestHistorical( m.historical_news_request().contract_id(), m.historical_news_request().provider_codes(), m.historical_news_request().total_results(), m.historical_news_request().start(), m.historical_news_request().end(), {sessionKey, clientId=m.historical_news_request().id(), _webSendPtr} );
			else if( m.has_implied_volatility() )
				CalculateImpliedVolatility( m.implied_volatility().contracts(), {sessionKey, clientId=m.implied_volatility().id()} );
			else if( m.has_implied_price() )
				CalculateImpliedPrice( m.implied_price().contracts(), {sessionKey, clientId=m.implied_price().id()} );
			else
				ERR( "Unknown Message '{}'", m.Value_case() );
		}
		catch( const IBException& e )
		{
			_webSendPtr->Push( e, {sessionKey, clientId} );
		}
		catch( const IException& e )
		{
			_webSendPtr->Push( e, {sessionKey, clientId} );
		}
	}

	::Contract TwsSendWorker::GetContract( ContractPK contractId )noexcept(false)
	{
		var pDetails = _tws.ReqContractDetails( contractId ).get();
		THROW_IF( pDetails->size()!=1, "contractId={} returned {} records"sv, contractId, pDetails->size() );
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
	α TwsSendWorker::RequestAllOpenOrders( ClientKey t )noexcept->Task2
	{
		try
		{
			var p = ( co_await Tws::RequestAllOpenOrders() ).Get<Proto::Results::Orders>();
			auto pResults = mu<Proto::Results::Orders>(); pResults->set_request_id( t.ClientId );
			for( var& order : p->orders() )
			{
				//var& o = order.order();
				//UM::TestAccess( EAccess::Read, client.UserId, sv tableName )noexcept(false)->void; //TODO check permissions
				*pResults->add_orders() = order;
			}
			for( var& s : p->statuses() )
			{
				*pResults->add_statuses() = s;
			}
			MessageType m; m.set_allocated_orders( pResults.release() );
			_webSendPtr->Push( move(m), t.SessionId );
		}
		catch( IException& e )
		{
			_webSendPtr->Push( e, t );
		}
	}
	α TwsSendWorker::HistoricalData( Proto::Requests::RequestHistoricalData r, SessionKey session )noexcept(false)->Task2
	{
		try
		{
			var pContract = ( co_await Tws::ContractDetails(r.contract().id()) ).Get<Contract>();
			var endDate = Chrono::ToDays( Clock::from_time_t(r.date()) );
			LOG( "({})HistoricalData( '{}', '{}', {}, '{}', '{}', {} )", session.SessionId, pContract->Symbol, DateDisplay(endDate), r.days(), BarSize::ToString(r.bar_size()), TwsDisplay::ToString(r.display()), r.use_rth() );
			var pBars = ( co_await Tws::HistoricalData(pContract, endDate, r.days(), r.bar_size(), r.display(), r.use_rth()) ).Get<vector<::Bar>>();
			auto p = mu<Proto::Results::HistoricalData>(); p->set_request_id( r.id() );
			for( var& bar : *pBars )
			{
				var time = bar.time.size()==8 ? DateTime{ (uint16)stoi(bar.time.substr(0,4)), (uint8)stoi(bar.time.substr(4,2)), (uint8)stoi(bar.time.substr(6,2)) } : DateTime( stoi(bar.time) );
				auto& proto = *p->add_bars();
				proto.set_time( (int)time.TimeT() );

				proto.set_high( bar.high );
				proto.set_low( bar.low );
				proto.set_open( bar.open );
				proto.set_close( bar.close );
				proto.set_wap( ToDouble(bar.wap) );
				proto.set_volume( bar.volume );
				proto.set_count( bar.count );
			}
			MessageType msg; 	msg.set_allocated_historical_data( p.release() );
			_web.Push( move(msg), session.SessionId );
		}
		catch( const IException& )
		{}
	}

	α TwsSendWorker::Executions( const Proto::Requests::RequestExecutions& r, const SessionKey& session )noexcept->void
	{
		_web.AddExecutionRequest( session.SessionId );

		DateTime time{ r.time() };
		var timeString = format( "{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", time.Year(), time.Month(), time.Day(), time.Hour(), time.Minute(), time.Second() );
		ExecutionFilter filter; filter.m_clientId=r.client_id(); filter.m_acctCode=r.account_number(); filter.m_time=timeString; filter.m_symbol=r.symbol(); filter.m_secType=r.security_type(); filter.m_exchange=r.exchange(); filter.m_side=r.side();
		_tws.reqExecutions( _web.AddRequestSession({{session.SessionId},r.id()}), filter );
	}

	α TwsSendWorker::Order( const Proto::Requests::PlaceOrder& r, const SessionKey& session )noexcept->void
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
			catch( const IException& e )
			{
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

	α TwsSendWorker::Request( const Proto::Requests::GenericRequest& r, const SessionKey& t )noexcept->void
	{
		if( r.type()==ERequests::RequestOptionParams )
			RequestOptionParams( r.id(), (int)r.item_id(), move(t) );
		else if( r.type()==ERequests::RequestAllOpenOrders )
			RequestAllOpenOrders( {t, r.id()} );
		else
			WARN( "({}.{})Unknown message '{}' - not forwarding to tws.", t.SessionId, r.id(), r.type() );
	}

	void TwsSendWorker::Requests( const Proto::Requests::GenericRequests& r, const SessionKey& session )noexcept
	{
		if( !_twsPtr )
			return _web.PushError( -1, "Not connected to Tws.", {{session.SessionId}, r.id()} );
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
		else if( r.type()==ERequests::ReqNewsProviders )
			News::RequestProviders( ProcessArg{session, r.id(), _webSendPtr} );
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

	α TwsSendWorker::RequestOptionParams( ClientPK clientId, google::protobuf::int32 underlyingId, SessionKey key )noexcept->Task2
	{
		try
		{
			auto p = ( co_await Tws::SecDefOptParams(underlyingId) ).Get<Proto::Results::OptionExchanges>();
			p->set_request_id( clientId );
			MessageType m; m.set_allocated_option_exchanges( new Proto::Results::OptionExchanges(*p) );
			_web.Push( move(m), key.SessionId );
		}
		catch( const IException& e )
		{
			_web.Push( e, ClientKey{key,clientId} );
		}
	}
}
