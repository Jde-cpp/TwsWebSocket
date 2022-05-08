#include "TwsSendWorker.h"
#include <jde/markets/types/MyOrder.h>
#include <jde/markets/types/proto/ResultsMessage.h>
#include "../../Framework/source/io/ProtoUtilities.h"
#include "../../Framework/source/Cache.h"
#include "../../MarketLibrary/source/TickManager.h"
#include "../../MarketLibrary/source/types/Exchanges.h"
#include "WebCoSocket.h"
#include "WebRequestWorker.h"
#include "requests/News.h"

#define var const auto
#define _tws (*_twsPtr)
#define _web ( *WebCoSocket::Instance()->WebSend() )
#define _cache (*static_pointer_cast<TwsClientCache>(_twsPtr))

#define TwsPtr(sessionId, clientId) auto pTws = static_pointer_cast<TwsClientCache>(_twsPtr); if( !pTws ) return _web.PushError( "Server not connected to TWS.", {{sessionId}, clientId} )

namespace Jde::Markets::TwsWebSocket
{
	static const LogTag& _logLevel = Logging::TagLevel( "app-webRequests" );
	α ManagedAccounts( sp<WebSendGateway> pWebClient, SessionKey key )noexcept->Task;

	TwsSendWorker::TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept:
		_webSendPtr{ webSendPtr },
		_twsPtr{ pTwsClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "TwsSendWorker", [&](){Run();} );
	}
	α TwsSendWorker::Push( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionInfo )noexcept->void
	{
		_queue.Push( {sessionInfo, pData} );
	}
	α TwsSendWorker::Run()noexcept->void
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.Empty() )
		{
			if( auto p = _queue.WaitAndPop(5s); p )
				HandleRequest( get<1>(*p), get<0>(*p) );
		}
	}
	α ContractDetails( Proto::Requests::RequestContractDetails r, SessionKey s )noexcept->Task;

	α Order( const Proto::Requests::PlaceOrder r, SessionKey s )noexcept->Task
	{
		const Contract c{ r.contract() };
		MyOrder o{ Tws::RequestId(), r.order() };
		LOG( "({}.{}) placeOrder {}", s.SessionId, r.id(), o.ToString(c.Display()) );
		WebSendGateway::AddOrder( o.orderId, s.SessionId );
		try
		{
			auto p = ( co_await Tws::PlaceOrder(c.ToTws(), move(o), r.block_id(), r.stop(), r.stop_limit()) ).SP<Proto::Results::OpenOrder>();
			WebSendGateway::PushS( ToMessage(r.id(), new Proto::Results::OpenOrder{*p}), s.SessionId );
		}
		catch( IException& e )
		{
			string message = e.Code==451 || e.Code==463 ? e.What() : "Place order failed";
			WebSendGateway::PushS( message, e, {{s.SessionId}, r.id()} );
		}
	}

	α TwsSendWorker::HandleRequest( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionKey )noexcept->void
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
				ContractDetails( move(m.contract_details()), move(sessionKey) );
			else if( m.has_historical_data() )
				HistoricalData( move(*m.mutable_historical_data()), sessionKey );
			else if( m.has_place_order() )
				Order( move(m.place_order()), move(sessionKey) );
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
			_webSendPtr->Push( "Request failed", e, {sessionKey, clientId} );
		}
		catch( const IException& e )
		{
			_webSendPtr->Push( "Request failed", e, {sessionKey, clientId} );
		}
	}

	α TwsSendWorker::GetContract( ContractPK contractId )noexcept(false)->const ::Contract&
	{
		var pDetails =  SFuture<::ContractDetails>( Tws::ContractDetail(contractId) ).get();
		return pDetails->contract;
	}

	α TwsSendWorker::CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& c )noexcept(false)->void
	{
		for( var& r : requests )
			TickManager::CalcImpliedVolatility( c.SessionId, c.ClientId, GetContract(r.contract_id()), r.option_price(), r.underlying_price(), [this,s=c.SessionId](var m){ _webSendPtr->Push(m,s);} );
	}

	α TwsSendWorker::CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& c )noexcept->void
	{
		for( var& r : requests )
			TickManager::CalculateOptionPrice( c.SessionId, c.ClientId, GetContract(r.contract_id()), r.volatility(), r.underlying_price(), [this,s=c.SessionId](var m){ _webSendPtr->Push(m,s);} );
	}

	α ContractDetails( Proto::Requests::RequestContractDetails r, SessionKey s )noexcept->Task
	{
		LOG( "({}.{}) ContractDetails count={}", s.SessionId, r.id(), r.contracts_size() );
		auto threadId = std::this_thread::get_id();
		vector<MessageType> messages; messages.reserve( r.contracts_size()+1 );
		for( int i=0; i<r.contracts_size(); ++i )
		{
			auto pResult = mu<Proto::Results::ContractDetailsResult>(); pResult->set_request_id( r.id() );
			const Contract contract{ r.contracts(i) };//303019419
			try
			{
				if( !contract.Id && contract.SecType==SecurityType::Option )
				{
					var pDetail = ( co_await Tws::ContractDetails(ToIb(contract.Symbol, contract.Expiration, contract.Right)) ).UP<vector<sp<::ContractDetails>>>();
					for( var p : *pDetail )
						*pResult->add_details() = ToProto( *p );
				}
				else
				{
					var pDetail = ( co_await Tws::ContractDetail(*contract.ToTws()) ).SP<::ContractDetails>();
					*pResult->add_details() = ToProto( *pDetail );
				}
			}
			catch( IException& )
			{
				pResult->add_details()->set_allocated_contract( contract.ToProto().release() );
			}

			messages.emplace_back().set_allocated_contract_details( pResult.release() );
			if( i==r.contracts_size()-1 || threadId!=std::this_thread::get_id() )
			{
				if( i==r.contracts_size()-1 )
				{
					auto pMessage = mu<Proto::Results::MessageValue>(); pMessage->set_int_value( r.id() ); pMessage->set_type( EResults::MultiEnd );
					messages.emplace_back().set_allocated_message( pMessage.release() );
				}
				else
					threadId = std::this_thread::get_id();
				_web.Push( move(messages), s.SessionId );
			}
		}
	}
	α TwsSendWorker::RequestAllOpenOrders( ClientKey t )noexcept->Task
	{
		LOG( "({}.{})RequestAllOpenOrders()", t.SessionId, t.ClientId );
		try
		{
			var p = ( co_await Tws::RequestAllOpenOrders() ).UP<Proto::Results::Orders>();
			auto pResults = mu<Proto::Results::Orders>(); pResults->set_request_id( t.ClientId );
			for( var& order : p->orders() )
				*pResults->add_orders() = order;
			for( var& s : p->statuses() )
				*pResults->add_statuses() = s;
			_webSendPtr->Push( ToMessage(pResults.release()), t.SessionId );
		}
		catch( IException& e )
		{
			_webSendPtr->Push( "Request failed", e, t );
		}
	}

	α AverageVolume( google::protobuf::RepeatedField<google::protobuf::int32> contractIds, ClientKey c )noexcept->Task
	{
		LOG( "({}.{})AverageVolume( '{}' )", c.SessionId, c.ClientId, contractIds.size()<5 ? Str::AddCommas(contractIds) : std::to_string(contractIds.size()) );
		for( var id : contractIds )
		{
			try
			{
				var pDetails = ( co_await Tws::ContractDetail(id) ).SP<::ContractDetails>();
				auto pContract = ms<Contract>( *pDetails );
				auto cacheId = format( "AverageVolume.{}", pContract->Symbol );
				auto average = Cache::Double( cacheId );
				if( isnan(average)  )
				{
					var current = CurrentTradingDay( *pContract ); var prev = PreviousTradingDay( current );
					var pBars = ( co_await Tws::HistoricalData(pContract, prev, 65, EBarSize::Day, EDisplay::Trades, false) ).SP<vector<::Bar>>();
					double sum{0};
					for_each( pBars->begin(), pBars->end(), [&](var& bar){ sum+=ToDouble(bar.volume); } );
					average = sum*100/pBars->size();
					Cache::SetDouble( move(cacheId), average, ExtendedBegin(*pContract, NextTradingDay(current)) );
				}
				WebSendGateway::PushS( ToMessage(EResults::AverageVolume, c.ClientId, id, average), c.SessionId );
			}
			catch( IException& )
			{
				WebSendGateway::PushS( ToMessage(EResults::AverageVolume, c.ClientId, id, 0), c.SessionId );
			}
		}
		if( contractIds.size()>1 )
		{
			auto p = mu<Proto::Results::ContractValue>(); p->set_type( EResults::AverageVolume ); p->set_request_id( c.ClientId ); p->set_contract_id( 0 );
			MessageType msg; 	msg.set_allocated_contract_value( p.release() );
			_web.Push( move(msg), c.SessionId );
		}
	}

	α TwsSendWorker::HistoricalData( Proto::Requests::RequestHistoricalData r, SessionKey s )noexcept(false)->Task
	{
		try
		{
			var pDetails = ( co_await Tws::ContractDetail(r.contract().id()) ).SP<::ContractDetails>();
			var endDate = Chrono::ToDays( Clock::from_time_t(r.date()) );
			LOG( "({}.{})HistoricalData( '{}', '{}', days={}, '{}', '{}', {} )", s.SessionId, r.id(), pDetails->contract.symbol, DateDisplay(endDate), r.days(), BarSize::ToString(r.bar_size()), TwsDisplay::ToString(r.display()), r.use_rth() );
			var pBars = ( co_await Tws::HistoricalData(ms<Contract>(*pDetails), endDate, r.days(), r.bar_size(), r.display(), r.use_rth()) ).SP<vector<::Bar>>();
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
			_web.Push( move(msg), s.SessionId );
		}
		catch( IException& e )
		{
			_web.Push( "Could not load historical data", e, {{s.SessionId}, r.id()} );
		}
	}

	α TwsSendWorker::Executions( const Proto::Requests::RequestExecutions& r, const SessionKey& session )noexcept->void
	{
		_web.AddExecutionRequest( session.SessionId );

		DateTime time{ r.time() };
		var timeString = format( "{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", time.Year(), time.Month(), time.Day(), time.Hour(), time.Minute(), time.Second() );
		ExecutionFilter filter; filter.m_clientId=r.client_id(); filter.m_acctCode=r.account_number(); filter.m_time=timeString; filter.m_symbol=r.symbol(); filter.m_secType=r.security_type(); filter.m_exchange=r.exchange(); filter.m_side=r.side();
		_tws.reqExecutions( _web.AddRequestSession({{session.SessionId},r.id()}), filter );
	}

	α TwsSendWorker::Positions( const Proto::Requests::RequestPositions& r, const SessionKey& session )noexcept->void
	{
		_tws.reqPositionsMulti( _web.AddRequestSession({{session.SessionId},r.id()}), r.account_number(), r.model_code() );
	}
	α LatestOrder( ::OrderId id, SessionPK s )noexcept->Task
	{
		var p = ( co_await OrderManager::Latest(id) ).UP<OrderManager::Cache>();
		if( !p )
			_web.PushError( "Order not found", {{s}, (ClientPK)id}, -1 );
		else
		{
			ASSERT( p->ContractPtr && p->OrderPtr && p->StatePtr );
			auto o = mu<Proto::Results::OpenOrder>();
			o->set_request_id( id );
			o->set_allocated_contract( p->ContractPtr->ToProto().release() );
			o->set_allocated_order( p->OrderPtr->ToProto().release() );
			o->set_allocated_state( ToProto(*p->StatePtr).release() );
			Proto::Results::MessageUnion m; m.set_allocated_open_order( o.release() );
			_web.Push( move(m), s );
		}
	}
	α TwsSendWorker::Request( const Proto::Requests::GenericRequest& r, const SessionKey& s )noexcept->void
	{
		var t = r.type();
		if( t==ERequests::RequestOptionParams )
			RequestOptionParams( (int)r.item_id(), {s, r.id()} );
		else if( t==ERequests::RequestAllOpenOrders )
			RequestAllOpenOrders( {s, r.id()} );
		else if( t==ERequests::Order )
		{
			LOG( "({}.{})LatestOrder( {} )", s.SessionId, r.id(), r.item_id() );
			LatestOrder( (::OrderId)r.item_id(), s.SessionId );
		}
		else
			WARN( "({}.{})Unknown message '{}' - not forwarding to tws.", s.SessionId, r.id(), r.type() );
	}

	α TwsSendWorker::Requests( const Proto::Requests::GenericRequests& r, const SessionKey& s )noexcept->void
	{
		if( !_twsPtr )
			return _web.PushError( "Not connected to Tws.", {{s.SessionId}, r.id()} );
		if( r.type()==ERequests::Positions )
		{
			_web.AddRequestSessions( s.SessionId, {EResults::PositionEnd, EResults::PositionData} );
			//if( _web.Add(ERequests::Positions) ) TODO test how this works with multiple calls.  and if canceled appropriately.
				//_tws.reqPositions();
			ERR( "reqPositions not implemented."sv );
			_web.PushError( "reqPositions not implemented.", {{s.SessionId}, r.id()} );
		}
		else if( r.type()==ERequests::ManagedAccounts )
			ManagedAccounts( _webSendPtr, s );
		else if( r.type()==ERequests::RequestOpenOrders )
		{
			LOG( "({})RequestOpenOrders", s.SessionId );
			_web.AddRequestSessions( s.SessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_tws.reqOpenOrders();
		}
		else if( r.type()==ERequests::CancelMarketData )
		{
			for( var contractId : r.ids() )
			{
				LOG( "({}.{})CancelMarketData - {}", s.SessionId, r.id(), contractId );
				TickManager::CancelProto( s.SessionId, 0, contractId );//~~~
			}
		}
		else if( r.type()==ERequests::CancelPositionsMulti )
		{
			for( var clientId : r.ids() )
			{
				LOG( "({}.{})CancelPositionsMulti", s.SessionId, clientId );
				var ibId = _web.RequestFind( {{s.SessionId}, (ClientPK)clientId} );
				if( ibId )
				{
					_tws.cancelPositionsMulti( ibId );
					_web.RequestErase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'"sv, s.SessionId, clientId );
			}
		}
		else if( r.type()==ERequests::CancelOrder )
		{
			LOG( "({}.{})CancelOrders( {} )", s.SessionId, r.id(), Str::AddCommas(r.ids()) );
			for( auto i=0; i<r.ids_size(); ++i )
			{
				var orderId{ r.ids(i) };
				_web.AddOrderSubscription( orderId, s.SessionId );
				_tws.cancelOrder( orderId );
			}
		}
		else if( r.type()==ERequests::ReqNewsProviders )
			News::RequestProviders( ProcessArg{s, r.id(), _webSendPtr} );
		else if( r.type()==ERequests::AverageVolume )
			AverageVolume( r.ids(), {s, r.id()} );
		else
			WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, r.type(), s.SessionId );
	}

	α TwsSendWorker::AccountUpdates( const Proto::Requests::RequestAccountUpdates& accountUpdates, const SessionKey& session )noexcept->void
	{
		var& account = accountUpdates.account_number();
		var subscribe = accountUpdates.subscribe();
		if( subscribe )
			_web.AddAccountSubscription( account, session.SessionId );
		else
			_web.CancelAccountSubscription( account, session.SessionId );
	}

	α TwsSendWorker::AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& r, const SessionKey& session )noexcept->void
	{
		var reqId =  _web.AddRequestSession( {{session.SessionId},r.id()} );
		var& account = r.account_number();

		DBG( "reqAccountUpdatesMulti( reqId='{}' sessionId='{}', account='{}', clientId='{}' )"sv, reqId, session.SessionId, account, r.id() );
		_tws.reqAccountUpdatesMulti( reqId, account, r.model_code(), r.ledger_and_nlv() );
	}
	α TwsSendWorker::MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& r, const SessionKey& s )noexcept->void
	{
		flat_set<Proto::Requests::ETickList> ticks;
		std::for_each( r.tick_list().begin(), r.tick_list().end(), [&ticks]( auto item ){ ticks.emplace((Proto::Requests::ETickList)item); } );
		//_webSendPtr->AddMarketDataSubscription( s.SessionId, r.contract_id(), ticks );
		try
		{
			TickManager::Subscribe( s.SessionId, 0, r.contract_id(), ticks, r.snapshot(), [this,s2=s.SessionId](var& m){ _webSendPtr->Push(m,s2);} );
		}
		catch( IException& e )
		{
			_webSendPtr->Push( "Market data request failed", e, {s,r.id()} );
		}
	}

	α TwsSendWorker::RequestOptionParams( google::protobuf::int32 underlyingId, ClientKey c )ι->Task
	{
		LOG( "({}.{})RequestOptionParams( {} )", c.SessionId, c.ClientId, underlyingId );
		try
		{
			auto p = ( co_await Tws::SecDefOptParams(underlyingId) ).SP<Proto::Results::OptionExchanges>();
			_web.Push( ToMessage(c.ClientId, new Proto::Results::OptionExchanges(*p)), c.SessionId );
		}
		catch( const IException& e )
		{
			_web.Push( "RequestOptionParams failed.", e, c );
		}
	}

	α ManagedAccounts( sp<WebSendGateway> pWebClient, SessionKey key )noexcept->Task
	{
		var p = ( co_await Tws::ReqManagedAccts() ).SP<vector<Account>>();
		auto accounts = mu<Proto::Results::StringMap>(); accounts->set_result(EResults::ManagedAccounts);
		for( var& account : *p )
		{
			if( Accounts::CanRead(account.IbName, key.UserId) )
				(*accounts->mutable_values())[account.IbName.size() ? account.IbName : account.Display] = account.Display;
		}
		MessageType m; m.set_allocated_string_map( accounts.release() );
		pWebClient->Push( move(m), key.SessionId );
	}
}