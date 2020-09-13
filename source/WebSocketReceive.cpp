#include "./WebSocket.h"
#include <Execution.h>
#include "./News.h"
#include "EWebReceive.h"
#include "./WatchListData.h"
#include "./WrapperWeb.h"
#include "../../Framework/source/Cache.h"
#include "../../MarketLibrary/source/types/IBException.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../MarketLibrary/source/data/OptionData.h"
#include "../../MarketLibrary/source/types/Bar.h"
#include "../../MarketLibrary/source/types/Contract.h"
#include "../../MarketLibrary/source/types/Exchanges.h"
#include "../../MarketLibrary/source/types/MyOrder.h"

#include "Flex.h"
#define var const auto

namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>
#define _sync TwsClientSync::Instance()
#define _client dynamic_cast<TwsClientCache&>(TwsClientSync::Instance())
namespace Jde::Markets::TwsWebSocket
{
	void WebSocket::DoSession( shared_ptr<Stream> pSession )noexcept
	{
		var sessionId = ++_sessionId;
		{
			Threading::SetThreadDescription( format("webReceive - {}", sessionId) );
			//IO::OStreamBuffer buffer( std::make_unique<std::vector<char>>(8192) );
			//td::ostream os( &buffer );
			try
			{
				pSession->accept();
				_sessions.emplace( sessionId, pSession );
			}
			catch( const boost::system::system_error& se )
			{
				if( se.code()==websocket::error::closed )
					DBG( "Socket Closed on session {} when sending acceptance."sv, sessionId );
				if( se.code() != websocket::error::closed )
					ERR( "Error sending app - {}."sv, se.code().message() );
			}
			catch( const std::exception& e )
			{
				ERR0( string(e.what()) );
			}
		}
		if( !_sessions.Find(sessionId) )
			return;
		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_type( Proto::Results::EResults::Accept ); pMessage->set_int_value( sessionId );
		PushAllocated( sessionId, pMessage );
		DBG( "Listening to session #{}"sv, sessionId );
		UnPause();
		while( !Threading::GetThreadInterruptFlag().IsSet() )
		{
			try
			{
				boost::beast::multi_buffer buffers;
				//vector<char> baseBuffer{'\0','\0','\0','\0'};
				//auto buffer = boost::asio::dynamic_buffer( baseBuffer );
				//boost::beast::ostream(buffer) << "\0\0\0\0";//std::array<char,4>{'\0','\0','\0','\0'};
				//var str2 = boost::beast::buffers_to( buffer.data() );
				pSession->read( buffers );
				//vector<google::protobuf::uint8> data;
				//data.reserve( boost::asio::buffer_size(buffers.data()) );
				// auto pData = buffers.data();//if in for loop causes crash in release mode.
				// for( boost::asio::const_buffer buffer : boost::beast::detail::buffers_range(pData) )
				// 	data.insert( std::end(data), static_cast<const char*>(buffer.data()), static_cast<const char*>(buffer.data())+buffer.size() );  //static_cast<const google::protobuf::uint8*>(buffer.data()), buffer.size() );
				var data = boost::beast::buffers_to_string( buffers.data() );
				google::protobuf::io::CodedInputStream input( reinterpret_cast<const unsigned char*>(data.data()), (int)data.size() );
				Proto::Requests::RequestTransmission transmission;
				if( !transmission.MergePartialFromCodedStream(&input) )
					THROW( IOException("transmission.MergePartialFromCodedStream returned false") );
				for( var& message : transmission.messages() )
				{
					if( message.has_account_updates() )
						ReceiveAccountUpdates( sessionId, message.account_updates() );
					else if( message.has_account_updates_multi() )
						ReceiveAccountUpdatesMulti( sessionId, message.account_updates_multi() );
					else if( message.has_generic_requests() )
						ReceiveRequests( sessionId, message.generic_requests() );
					else if( message.has_string_request() )
						Receive( sessionId, message.string_request().id(), message.string_request().type(), message.string_request().name() );
					else if( message.has_market_data_smart() )
						ReceiveMarketDataSmart( sessionId, message.market_data_smart() );
					else if( message.has_contract_details() )
						ReceiveContractDetails( sessionId, message.contract_details() );
					else if( message.has_options() )
						ReceiveOptions( sessionId, message.options() );
					else if( message.has_historical_data() )
						ReceiveHistoricalData( sessionId, message.historical_data() );
					else if( message.has_flex_executions() )
						ReceiveFlex( sessionId, message.flex_executions() );
					else if( message.has_place_order() )
						ReceiveOrder( sessionId, message.place_order() );
					else if( message.has_request_positions() )
						ReceivePositions( sessionId, message.request_positions() );
					else if( message.has_request_executions() )
						ReceiveExecutions( sessionId, message.request_executions() );
					else if( message.has_edit_watch_list() )
						WatchListData::Edit( sessionId, message.edit_watch_list().id(), message.edit_watch_list().file() );
					else if( message.has_news_article_request() )
						News::RequestArticle( sessionId, message.news_article_request().id(), message.news_article_request().provider_code(), message.news_article_request().article_id() );
					else if( message.has_historical_news_request() )
						News::RequestHistorical( sessionId, message.historical_news_request().id(), message.historical_news_request().contract_id(), message.historical_news_request().provider_codes(), message.historical_news_request().total_results(), message.historical_news_request().start(), message.historical_news_request().end() );

/*	xWatchLists							= -2;
	xWatchList							= -3;
	xPortfolios							= -4;
	xCreateWatchList					= -5;
	DeleteWatchList					= -6;
	EditWatchList						= -7;
*/
					else
						ERR( "Unknown Message '{}'"sv, message.Value_case() );
				}
			}
			catch( const IOException& e )
			{
				ERR( "IOExeption returned: '{}'"sv, e.what() );
			}
			catch(boost::system::system_error const& se)
			{
				auto code = se.code();
				if(  code == websocket::error::closed )
					DBG( "se.code()==websocket::error::closed, id={}"sv, sessionId );
				else
				{
					if( code.value()==104 )//reset by peer
						DBG( "system_error returned: '{}' - closing connection - {}"sv, se.code().message(), sessionId );
					else
						DBG( "system_error returned: '{}' - closing connection - {}"sv, se.code().message(), sessionId );
					EraseSession( sessionId );
					break;
				}
			}
			catch( std::exception const& e )
			{
				ERR( "std::exception returned: '{}'"sv, e.what() );
			}
			if( !_sessions.Find(sessionId) )
			{
				DBG( "Could not find session id {} exiting thread."sv, sessionId );
				break;
			}
		}
		DBG0( "Leaving WebSocket::DoSession"sv );
	}

	void WebSocket::AddRequestSessions( SessionId id, const vector<Proto::Results::EResults>& webSendMessages )noexcept
	{
		function<void(UnorderedSet<SessionId>&)> afterInsert = [id](UnorderedSet<SessionId>& values){values.emplace(id);};
		for( var sendMessage : webSendMessages )
			_requestSessions.Insert( afterInsert, sendMessage, shared_ptr<UnorderedSet<SessionId>>{new UnorderedSet<SessionId>{}} );
	}
	void WebSocket::Receive( SessionId sessionId, ClientRequestId requestId, ERequests type, const string& name )noexcept
	{
		if( type==ERequests::WatchList )
			WatchListData::SendList( sessionId, requestId, name );
		else if( type==ERequests::DeleteWatchList )
			WatchListData::Delete( sessionId, requestId, name );

	}
	void WebSocket::ReceiveRequests( SessionId sessionId, const Proto::Requests::GenericRequests& request )noexcept
	{
		if( request.type()==ERequests::Positions )
		{
			AddRequestSessions( sessionId, {EResults::PositionEnd, EResults::PositionData} );
			if( _requests.emplace(ERequests::Positions) )
				_client.reqPositions();
		}
		else if( request.type()==ERequests::ManagedAccounts )
		{
			AddRequestSessions( sessionId, {EResults::ManagedAccounts} );
			_client.reqManagedAccts();
		}
		else if( request.type()==ERequests::RequestOpenOrders )
		{
			AddRequestSessions( sessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_client.reqOpenOrders();
		}
		else if( request.type()==ERequests::RequestAllOpenOrders )
		{
			AddRequestSessions( sessionId, {EResults::OrderStatus_, EResults::OpenOrder_, EResults::OpenOrderEnd} );//TODO handle simultanious multiple requests
			_client.reqAllOpenOrders();
		}
		else if( request.type()==ERequests::CancelMarketData )
		{
			for( auto i=0; i<request.ids_size(); ++i )
			{
				auto ibId = FindRequestId( sessionId, request.ids(i) );
				if( ibId && WrapperWeb::Instance().AddCanceled(ibId) )
				{
					_client.cancelMktData( ibId );
					_requestSession.erase( ibId );
					std::unique_lock<std::shared_mutex> l{ _mktDataRequestsMutex };
					_mktDataRequests.erase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'"sv, sessionId, request.ids(i) );
			}
		}
		else if( request.type()==ERequests::CancelPositionsMulti )
		{
			for( auto i=0; i<request.ids_size(); ++i )
			{
				auto ibId = FindRequestId( sessionId, request.ids(i) );
				if( ibId )
				{
					_client.cancelPositionsMulti( ibId );
					_requestSession.erase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'"sv, sessionId, request.ids(i) );
			}
		}
		else if( request.type()==ERequests::CancelOrder )
		{
			for( auto i=0; i<request.ids_size(); ++i )
			{
				var reqId = request.ids(i);
				if( !_requestSession.emplace( reqId, make_tuple(sessionId,reqId) ) )
					DBG( "Cancel orderId {} 2x."sv, reqId );
				_client.cancelOrder( reqId );
			}
		}
		else if( request.type()==ERequests::RequestOptionParams )
			RequestOptionParams( sessionId, request.ids() );
		else if( request.type()==ERequests::RequsetPrevOptionValues )
			RequestPrevDayValues( sessionId, request.id(), request.ids() );
		else if( request.type()==ERequests::RequestFundamentalData )
			RequestFundamentalData( sessionId, request.id(), request.ids() );
		else if( request.type()==ERequests::Portfolios )
			WatchListData::SendLists( sessionId, request.id(), true );
		else if( request.type()==ERequests::WatchLists )
			WatchListData::SendLists( sessionId, request.id(), false );
		else if( request.type()==ERequests::ReqNewsProviders )
		{
			AddRequestSessions( sessionId, {EResults::NewsProviders} );
			_client.RequestNewsProviders();
		}
		else
			WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, request.type(), sessionId );
	}
	void WebSocket::RequestOptionParams( SessionId sessionId, const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds )noexcept
	{
		std::thread( [this,sessionId, ids=underlyingIds]()
		{
			try
			{
				for( auto i=0; i<ids.size(); ++i )
				{
					var underlyingId = ids[i];
					if( !underlyingId )
						THROW( Exception("Requested underlyingId=='{}'.", underlyingId) );
					var pDetails = _sync.ReqContractDetails(underlyingId).get();
					if( pDetails->size()!=1 )
						THROW( Exception("'{}' had '{}' contracts", underlyingId, pDetails->size()) );
					var requestId = _client.RequestId();
					_requestSession.emplace( requestId, make_tuple(sessionId,underlyingId) );
					_client.reqSecDefOptParams( requestId, underlyingId, pDetails->front().contract.localSymbol );
				}
			}
			catch( const Exception& e )
			{
				WebSocket::Instance().PushError( sessionId, 0, -1, e.what() );
			}
		}).detach();
	}
	void WebSocket::ReceiveHistoricalData( SessionId sessionId, const Proto::Requests::RequestHistoricalData& req )noexcept
	{
		var reqId = _client.RequestId();
		_requestSession.emplace( reqId, make_tuple(sessionId,req.id()) );
		Jde::Markets::Contract contract{ req.contract() };
		var time = Clock::from_time_t( req.date() );
		_client.ReqHistoricalData( reqId, contract, Chrono::DaysSinceEpoch(time), req.days(), req.bar_size(), req.display(), req.use_rth() );
	}
	void WebSocket::ReceiveFlex( SessionId sessionId, const Proto::Requests::FlexExecutions& req )noexcept
	{
		var start = Chrono::BeginningOfDay( Clock::from_time_t(req.start()) );
		var end = Chrono::EndOfDay( Clock::from_time_t(req.end()) );
		std::thread( [sessionId, clientId=req.id(), start, end, accountNumber=req.account_number()]()
		{
			Flex::SendTrades( sessionId, clientId, accountNumber, start, end );
		}).detach();
	}

	void WebSocket::ReceiveOrder( SessionId sessionId, const Proto::Requests::PlaceOrder& proto )noexcept
	{
		const ::OrderId reqId = proto.order().id() ? proto.order().id() : _client.RequestId();
		_requestSession.emplace( reqId, make_tuple(sessionId,proto.id()) );
		var pIbContract = Jde::Markets::Contract{ proto.contract() }.ToTws();
		const MyOrder order{ reqId, proto.order() };
		DBG( "({})receiveOrder( '{}', contract='{}' {}x{} )"sv, reqId, sessionId, pIbContract->symbol, order.lmtPrice, order.totalQuantity );
		_client.placeOrder( *pIbContract, order );
/*		if( !order.whatIf && proto.stop()>0 )
		{
			var parentId = _client.RequestId();
			_requestSession.emplace( parentId, make_tuple(sessionId, proto.id()) );
			Jde::Markets::MyOrder parent{ parentId, proto.order() };
			parent.IsBuy( !order.IsBuy() );
			parent.OrderType( Proto::EOrderType::StopLimit );
			parent.totalQuantity = order.totalQuantity;
			parent.auxPrice = proto.stop();
			parent.lmtPrice = proto.stop_limit();
			parent.parentId = reqId;
			_client.placeOrder( *pIbContract, parent );
		}*/
	}

	void WebSocket::ReceivePositions( SessionId sessionId, const Proto::Requests::RequestPositions& request )noexcept
	{
		var reqId = _client.RequestId();
		_requestSession.emplace( reqId, make_tuple(sessionId, request.id()) );
		_client.reqPositionsMulti( reqId, request.account_number(), request.model_code() );
	}

	void WebSocket::ReceiveExecutions( SessionId sessionId, const Proto::Requests::RequestExecutions& request )noexcept
	{
		var reqId = _client.RequestId();
		{
			std::shared_lock<std::shared_mutex> l( _executionRequestMutex );
			_executionRequests.emplace( sessionId );
		}

		_requestSession.emplace( reqId, make_tuple(sessionId, request.id()) );
		DateTime time{ request.time() };
		var timeString = format( "{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", time.Year(), time.Month(), time.Day(), time.Hour(), time.Minute(), time.Second() );
		ExecutionFilter filter; filter.m_clientId=request.client_id(); filter.m_acctCode=request.account_number(); filter.m_time=timeString; filter.m_symbol=request.symbol(); filter.m_secType=request.security_type(); filter.m_exchange=request.exchange(); filter.m_side=request.side();
		_client.reqExecutions( reqId, filter );
	}

	void WebSocket::ReceiveOptions( SessionId sessionId, const Proto::Requests::RequestOptions& options )noexcept
	{
		std::thread( [sessionId,options]()
		{
			Threading::SetThreadDescription( "ReceiveOptions" );
			var underlyingId = options.contract_id();
			try
			{
				//TODO see if I have, if not download it, else try getting from tws. (make sure have date.)
				if( underlyingId==0 )
					THROW( Exception("did not pass a contract id.") );
				var pDetails = _sync.ReqContractDetails( underlyingId ).get();
				if( pDetails->size()!=1 )
					THROW( Exception("'{}' had '{}' contracts", underlyingId, pDetails->size()) );
				var contract = Contract{ pDetails->front() };
				if( contract.SecType==SecurityType::Option )
					THROW( Exception("passed in option contract ({})'{}', expected underlying", underlyingId, contract.LocalSymbol) );

				var right = (SecurityRight)options.security_type();
				const std::array<string_view,4> longOptions{ "TSLA", "GLD", "SPY", "QQQ" };
				if( std::find(longOptions.begin(), longOptions.end(), contract.Symbol)!=longOptions.end() && !options.start_expiration() )
					THROW( Exception("ReceiveOptions request for '{}' - {} specified no date.", contract.Symbol, ToString(right)) );

				auto pResults = new Proto::Results::OptionValues(); pResults->set_id( options.id() );
				auto fetch = [&]( DayIndex expiration )noexcept(false)
				{
					var ibContract = TwsClientCache::ToContract( contract.Symbol, expiration, right, options.start_srike() && options.start_srike()==options.end_strike() ? options.start_srike() : 0 );
					auto pContracts = _sync.ReqContractDetails( ibContract ).get();
					if( pContracts->size()<1 )
						THROW( Exception("'{}' - '{}' {} has {} contracts", contract.Symbol, DateTime{Chrono::FromDays(expiration)}.DateDisplay(), ToString(right), pContracts->size()) );
					var start = options.start_srike(); var end = options.end_strike();
					if( !ibContract.strike && (start!=0 || end!=0) )
					{
						auto pContracts2 = make_shared<vector<ContractDetails>>();
						for( var& details : *pContracts )
						{
							if( (start==0 || start<=details.contract.strike) && (end==0 || end>=details.contract.strike) )
								pContracts2->push_back( details );
						}
						pContracts = pContracts2;
					}

					return OptionData::LoadDiff( contract, *pContracts, *pResults );
				};
				if( options.start_expiration()==options.end_expiration() )
					pResults->set_day( fetch(options.start_expiration()) );
				else
				{
					var optionParams = _sync.ReqSecDefOptParamsSmart( underlyingId, contract.Symbol );
					var end = options.end_expiration() ? options.end_expiration() : std::numeric_limits<DayIndex>::max();
					for( var expiration : optionParams.expirations() )
					{
						if( expiration<options.start_expiration() || expiration>end )
							continue;
						try
						{
							pResults->set_day( fetch(expiration) );
						}
						catch( const IBException& e )
						{
							if( e.ErrorCode!=200 )//No security definition has been found for the request
								throw e;
						}
					}
				}
				if( pResults->option_days_size() )
				{
					auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_options( pResults );
					WebSocket::Instance().Push( sessionId, pUnion );
				}
				else
					WebSocket::Instance().PushError( sessionId, underlyingId, -2, "No previous dates found" );
			}
			catch( const IBException& e )
			{
				WebSocket::Instance().Push( sessionId, options.id(), e );
			}
			catch( const Exception& e )
			{
				WebSocket::Instance().Push( sessionId, options.id(), e );
			}
		} ).detach();
	}

	void WebSocket::ReceiveContractDetails( SessionId sessionId, const Proto::Requests::RequestContractDetails& request )noexcept
	{
		var clientRequestId = request.id();
		unordered_set<TickerId> requestIds;
		for( int i=0; i<request.contracts_size(); ++i )
			requestIds.emplace( _client.RequestId() );
		{
			unique_lock l{_multiRequestMutex};
			_multiRequests.emplace( clientRequestId, requestIds );
		}
		int i=0;
		for( var reqId : requestIds )
		{
			const Contract contract{ request.contracts(i++) };//
			DBG( "reqContractDetails( reqId='{}' sessionId='{}', contract='{}' )"sv, reqId, sessionId, contract.Symbol );
			_requestSession.emplace( reqId, make_tuple(sessionId,clientRequestId) );
			if( contract.SecType==SecurityType::Option )
				_client.ReqContractDetails( reqId, TwsClientCache::ToContract(contract.Symbol, contract.Expiration, contract.Right) );
			else
				_client.ReqContractDetails( reqId, *contract.ToTws() );
		}
	}

	void WebSocket::ReceiveMarketDataSmart( SessionId sessionId, const Proto::Requests::RequestMrkDataSmart& request )noexcept
	{
		var reqId = _client.RequestId();
		ibapi::Contract contract; contract.conId = request.contract_id(); contract.exchange = "SMART";
		var ticks = StringUtilities::AddCommas( request.tick_list() );
		DBG( "receiveMarketDataSmart( reqId='{}' sessionId='{}', contract='{}' )"sv, reqId, sessionId, contract.conId );

		_requestSession.emplace( reqId, make_tuple(sessionId,request.id()) );
		{
			std::unique_lock<std::shared_mutex> l{ _mktDataRequestsMutex };
			auto insert = _mktDataRequests.emplace( reqId, unordered_set<SessionId>{sessionId} );
			if( !insert.second )
				insert.first->second.emplace( sessionId );
		}
		_client.reqMktData( reqId, contract, ticks, request.snapshot(), false, TagValueListSPtr() );
	}
	void WebSocket::ReceiveAccountUpdates( SessionId sessionId, const Proto::Requests::RequestAccountUpdates& accountUpdates )noexcept
	{
		var& account = accountUpdates.account_number();
		var subscribe = accountUpdates.subscribe();
		std::unique_lock<std::shared_mutex> l{ _accountRequestMutex };
		if( subscribe )
		{
			DBG( "({}) - subscribe account '{}'"sv, sessionId, account );
			auto [pInserted, inserted] = _accountRequests.emplace( account, unordered_set<SessionId>{} );
			pInserted->second.emplace( sessionId );
			if( inserted )
			{
				DBG( "Subscribe to account updates '{}'"sv, account );
				_client.reqAccountUpdates( true, account );
			}
		}
		else
		{
			DBG( "({}) - unsubscribe from account '{}'"sv, sessionId, account );
			auto pAccountSessionIds = _accountRequests.find( account );
			bool cancel = pAccountSessionIds==_accountRequests.end();
			if( !cancel )
			{
				pAccountSessionIds->second.erase( sessionId );
				cancel = pAccountSessionIds->second.size()==0;
			}
			if( cancel )
			{
				DBG( "Unsubscribe from account '{}'"sv, account );
				_accountRequests.erase( account );
				_client.reqAccountUpdates( false, account );
			}
		}//return & killed connection
	}
	void WebSocket::ReceiveAccountUpdatesMulti( SessionId sessionId, const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates )noexcept
	{
		var reqId =  _client.RequestId();
		var& account = accountUpdates.account_number();
		_requestSession.emplace( reqId, make_tuple(sessionId,accountUpdates.id()) );
		DBG( "reqAccountUpdatesMulti( reqId='{}' sessionId='{}', account='{}', clientId='{}' )"sv, reqId, sessionId, account, accountUpdates.id() );
		_client.reqAccountUpdatesMulti( reqId, account, accountUpdates.model_code(), accountUpdates.ledger_and_nlv() );
	}
	void WebSocket::RequestPrevDayValues( SessionId sessionId, ClientRequestId requestId, const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds )noexcept
	{
		std::thread( [this, current=PreviousTradingDay(), sessionId, contractIds, requestId]()
		{
			unordered_map<DayIndex,Proto::Results::DaySummary*> bars;
			try
			{
				for( var contractId : contractIds )
				{
					sp<vector<ContractDetails>> pDetails;
					try
					{
						pDetails = _sync.ReqContractDetails( contractId ).get();
						THROW_IF( pDetails->size()!=1, IBException( format("ReqContractDetails( {} ) returned {}."sv, contractId, pDetails->size()), -1) );
					}
					catch( const IBException& e )
					{
						THROW( IBException( format("ReqContractDetails( {} ) returned {}.", contractId, e.what()), e.ErrorCode, e.RequestId) );
					}

					Contract contract{ pDetails->front() };
					var isOption = contract.SecType==SecurityType::Option;
					if( contract.Symbol=="AXE" )
						DBG( "AXE conId='{}'"sv, contract.Id );
					var isPreMarket = IsPreMarket( contract.SecType );
					var count = IsOpen( contract.SecType ) && !isPreMarket ? 1 : 2;
					for( auto i=0; i<count; ++i )
					{
						var day = isPreMarket
							? i==0 ? current : NextTradingDay( current )
							: i==0 ? current : PreviousTradingDay( current );
						auto pBar1 = new Proto::Results::DaySummary{}; pBar1->set_request_id( requestId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
						bars.emplace( day, pBar1 );
					}
					auto load = [isPreMarket,isOption,count,current,sessionId,this,contract,&bars]( bool useRth, bool fillInBack )
					{
						auto groupByDay = []( const vector<::Bar>& ibBars )->map<DayIndex,vector<::Bar>>
						{
							map<DayIndex,vector<::Bar>> dayBars;
							for( var& bar : ibBars )
							{
								var day = Chrono::DaysSinceEpoch( Clock::from_time_t(ConvertIBDate(bar.time)) );
								dayBars.try_emplace( day, vector<::Bar>{} ).first->second.push_back( bar );
							}
							return dayBars;
						};
						auto set = [&bars, groupByDay]( const vector<::Bar>& ibBars, function<void(Proto::Results::DaySummary& summary, double close)> fnctn )
						{
							var dayBars = groupByDay( ibBars );
							for( var [day, pBar] : bars )
							{
								var pIbBar = dayBars.find( day );
								if( pIbBar!=dayBars.end() )
									fnctn( *pBar, pIbBar->second.back().close );
							}
						};
						var barSize = isOption ? Proto::Requests::BarSize::Hour : Proto::Requests::BarSize::Day;
						var endDate = fillInBack ? current : PreviousTradingDay( current );
						var dayCount = !useRth ? 1 : useRth && !fillInBack ? std::max( 1, count-1 ) : count;
						var pAsks = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Ask, useRth, true ).get();
						set( *pAsks, []( Proto::Results::DaySummary& summary, double close ){ summary.set_ask( close ); } );

						var pBids = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Bid, useRth, true ).get();
						set( *pBids, []( Proto::Results::DaySummary& summary, double close ){ summary.set_bid( close ); } );

						if( isPreMarket && fillInBack )
						{
							var today = NextTradingDay( current );
							var pToday = _sync.ReqHistoricalDataSync( contract, today, 1, barSize, TwsDisplay::Enum::Trades, false, true ).get();
							if( auto pDayBar = bars.find(today); pToday->size()==1 && pDayBar!=bars.end() )
							{
								var& bar = pToday->front();
								pDayBar->second->set_high( bar.high );
								pDayBar->second->set_low( bar.low );
								pDayBar->second->set_open( bar.open );
								auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_day_summary( pDayBar->second );
								Push( sessionId, pUnion );
								bars.erase( today );
								return;
							}
						}
						var setHighLowRth = !useRth && fillInBack;//after close & before open, want range/volume to be rth.
						if( setHighLowRth )
						{
							var pTrades = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true, true ).get();
							var dayBars = groupByDay( *pTrades );
							for( var [day, pBar] : bars )
							{
								var pIbBar = dayBars.find( day );
								if( pIbBar==dayBars.end() )
									continue;
								var& back = pIbBar->second.back();

								pBar->set_high( back.high );
								pBar->set_low( back.low );
								pBar->set_volume( back.volume );
							}
						}
						//if( contract.Symbol=="AAPL" )
						//	DBG0( "AAPL"sv );
						var pTrades = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true, useRth ).get();
						if( !pTrades )
							DBG( "{} - !pTrades"sv, contract.Symbol );
						//ASSERT( pTrades );
						var dayBars = groupByDay( *pTrades );
						vector<DayIndex> sentDays{}; sentDays.reserve( bars.size() );
						for( var [day, pBar] : bars )
						{
							var pIbBar = dayBars.find( day );
							if( pIbBar==dayBars.end() )
								continue;
							var& trades = pIbBar->second;
							pBar->set_count( static_cast<uint32>(trades.size()) );
							pBar->set_open( trades.front().open );
							var& back = trades.back();
							pBar->set_close( back.close );
							if( !setHighLowRth )
							{
								double high=0.0, low = std::numeric_limits<double>::max();
								for( var& bar : trades )
								{
									pBar->set_volume( pBar->volume()+bar.volume );
									high = std::max( bar.high, high );
									low = std::min( bar.low, low );
								}
								pBar->set_high( high );
								pBar->set_low( low==std::numeric_limits<double>::max() ? 0 : low );
							}
							auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_day_summary( pBar );
							sentDays.push_back( day );
							Push( sessionId, pUnion );
						}
						for( var day : sentDays )
							bars.erase( day );
					};
					var useRth = isOption || count==1;
					load( useRth, true );
					if( !useRth )
						load( !useRth, false );
				}
				Push( sessionId, requestId, Proto::Results::EResults::MultiEnd );
			}
			catch( const IBException& e )
			{
				std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
				Push( sessionId, requestId, e );
			}
		} ).detach();
	}
	void WebSocket::RequestFundamentalData( SessionId sessionId, ClientRequestId requestId, const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds )noexcept
	{
		std::thread( [this, current=PreviousTradingDay(), sessionId, contractIds, requestId]()
		{
			for( var contractId : contractIds )
			{
				sp<map<string,double>> pFundamentals;
				try
				{
					var pDetails = _sync.ReqContractDetails( contractId ).get(); THROW_IF( pDetails->size()!=1, IBException(format("{} has {} contracts", contractId, pDetails->size()), -1) );
					pFundamentals = _sync.ReqRatios( pDetails->front().contract ).get();
				}
				catch( const IBException& e )
				{
					Push( sessionId, requestId, e );
					return;
				}
				auto pRatios = new Proto::Results::Fundamentals();
				pRatios->set_request_id( requestId );
				for( var& [name,value] : *pFundamentals )
					(*pRatios->mutable_values())[name] = value;
				auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_fundamentals( pRatios );
				Push( sessionId, pUnion );
			}
			Push( sessionId, requestId, Proto::Results::EResults::MultiEnd );
		} ).detach();
	}
}