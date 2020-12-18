#include "WebRequestWorker.h"
#include "./BlocklyWorker.h"
#include "./Flex.h"
#include "./News.h"
#include "./PreviousDayValues.h"
#include "./WatchListData.h"
#include "./WebSocket.h"
#include "../../MarketLibrary/source/data/HistoricalDataCache.h"
#include "../../MarketLibrary/source/data/OptionData.h"

#define _sync TwsClientSync::Instance()
#define var const auto
namespace Jde::Markets::TwsWebSocket
{
	WebRequestWorker::WebRequestWorker( /*WebSocket& webSocketParent,*/ sp<WebSendGateway> webSend, sp<TwsClientSync> pTwsClient )noexcept:
		_pTwsSend{ make_shared<TwsSendWorker>(webSend, pTwsClient) },
		_pWebSend{webSend},
		_pBlocklyWorker{ make_shared<BlocklyWorker>(_pWebSend) }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebRequestWorker", [&](){Run();} );
	}
	void WebRequestWorker::Push( SessionPK sessionId, string&& data )noexcept
	{
		QueueType x = std::make_tuple( sessionId, std::move(data) );
		_queue.Push( std::move(x) );
	}
	void WebRequestWorker::Run()noexcept
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.empty() )
		{
			if( auto v =_queue.TryPop(5s); v )
				HandleRequest( get<0>(*v), std::move(get<1>(*v)) );
		}
	}
#define ARG(x) {{sessionId, x}, _pWebSend}
	void WebRequestWorker::HandleRequest( SessionPK sessionId, string&& data )noexcept
	{
		try
		{
			Proto::Requests::RequestTransmission transmission;
			if( google::protobuf::io::CodedInputStream  stream{reinterpret_cast<const unsigned char*>(data.data()), (int)data.size()}; !transmission.MergePartialFromCodedStream(&stream) )
				THROW( IOException("transmission.MergePartialFromCodedStream returned false") );
			while( transmission.messages().size() )
			{
				auto pMessage = sp<Proto::Requests::RequestUnion>( transmission.mutable_messages()->ReleaseLast() );
				var& message = *pMessage;
				if( message.has_string_request() )
					Receive( message.string_request().type(), message.string_request().name(), {sessionId, message.string_request().id()} );
				else if( message.has_options() )
					ReceiveOptions( sessionId, message.options() );
				else if( message.has_flex_executions() )
					ReceiveFlex( sessionId, message.flex_executions() );
				else if( message.has_edit_watch_list() )
					WatchListData::Edit( message.edit_watch_list().file(), ARG(message.edit_watch_list().id()) );
				else if( pMessage->has_blockly() )
				{
					auto p=pMessage->mutable_blockly();
					//string_view msg = p->message();
					//if( has && msg.size() )
						_pBlocklyWorker->Push( {sessionId, p->id(), up<string>{p->release_message()}} );
					//else
					//	_pWebSend->PushError( -5, "No Blockly msg", {sessionId, p->id()} );
				}
				else if( message.has_std_dev() )
					ReceiveStdDev( message.std_dev().contract_id(), message.std_dev().days(), message.std_dev().start(), ARG(message.std_dev().id()) );
				else
				{
					bool handled = false;
					if( message.has_generic_requests() )
						handled = ReceiveRequests( sessionId, message.generic_requests() );
					if( !handled )
						_pTwsSend->Push( pMessage, sessionId );
				}
			}
		}
		catch( const IOException& e )
		{
			ERR( "IOExeption returned: '{}'"sv, e.what() );
			_pWebSend->Push( e, {sessionId, 0} );
		}
	}

	void WebRequestWorker::Receive( ERequests type, const string& name, const ClientKey& arg )noexcept
	{
		if( type==ERequests::WatchList )
			WatchListData::SendList( name, {arg, _pWebSend} );
		else if( type==ERequests::DeleteWatchList )
			WatchListData::Delete( name, {arg, _pWebSend} );

	}
	bool WebRequestWorker::ReceiveRequests( SessionPK sessionId, const Proto::Requests::GenericRequests& request )noexcept
	{
		bool handled = true;
		if( request.type()==ERequests::RequsetPrevOptionValues )
			PreviousDayValues( request.ids(), ARG(request.id()) );
		else if( request.type()==ERequests::RequestFundamentalData )
			RequestFundamentalData( request.ids(), {sessionId, request.id()} );
		else if( request.type()==ERequests::Portfolios )
			WatchListData::SendLists( true, ARG(request.id()) );
		else if( request.type()==ERequests::WatchLists )
			WatchListData::SendLists( false, ARG(request.id()) );
		else
			handled = false;//WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, request.type(), sessionId );
		return handled;

	}
	void WebRequestWorker::ReceiveFlex( SessionPK sessionId, const Proto::Requests::FlexExecutions& req )noexcept
	{
		var start = Chrono::BeginningOfDay( Clock::from_time_t(req.start()) );
		var end = Chrono::EndOfDay( Clock::from_time_t(req.end()) );
		std::thread( [web=ProcessArg ARG(req.id()), start, end, accountNumber=req.account_number(), pWebSend=_pWebSend]()
		{
			Flex::SendTrades( accountNumber, start, end, web );
		}).detach();
	}

	void WebRequestWorker::ReceiveStdDev( ContractPK contractId, double days, DayIndex start, const ProcessArg& inputArg )noexcept
	{
		std::thread( [contractId, days, start, inputArg]()
		{
			var pDetails = _sync.ReqContractDetails( contractId ).get();
			try
			{
				if( pDetails->size()!=1 )
					THROW( Exception("Contract '{}' return '{}' records"sv, contractId, pDetails->size()) );
				var pStats = HistoricalDataCache::ReqStats( {pDetails->front()}, days, start );
				auto p = new Proto::Results::Statistics(); p->set_request_id(inputArg.ClientId); p->set_count(static_cast<uint32>(pStats->Count)); p->set_average(pStats->Average);p->set_variance(pStats->Variance);p->set_min(pStats->Min); p->set_max(pStats->Max);
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_statistics( p );
				inputArg.Push( pUnion );
			}
			catch( const Exception& e )
			{
				inputArg.Push( e );
			}
		} ).detach();
	}

	void WebRequestWorker::ReceiveOptions( SessionPK sessionId, const Proto::Requests::RequestOptions& params )noexcept
	{
		std::thread( [params, web=ProcessArg ARG(params.id())]()
		{
			Threading::SetThreadDscrptn( "ReceiveOptions" );
			var underlyingId = params.contract_id();
			try
			{
				//TODO see if I have, if not download it, else try getting from tws. (make sure have date.)
				if( underlyingId==0 )
					THROW( Exception("({}.{}) did not pass a contract id.", web.SessionId, web.ClientId) );
				var pDetails = _sync.ReqContractDetails( underlyingId ).get();
				if( pDetails->size()!=1 )
					THROW( Exception("({}.{}) '{}' had '{}' contracts", web.SessionId, web.ClientId, underlyingId, pDetails->size()) );
				var contract = Contract{ pDetails->front() };
				if( contract.SecType==SecurityType::Option )
					THROW( Exception("({}.{})Passed in option contract ({})'{}', expected underlying", web.SessionId, web.ClientId, underlyingId, contract.LocalSymbol) );

				const std::array<string_view,4> longOptions{ "TSLA", "GLD", "SPY", "QQQ" };
				if( std::find(longOptions.begin(), longOptions.end(), contract.Symbol)!=longOptions.end() && !params.start_expiration() )
					THROW( Exception("({}.{})ReceiveOptions request for '{}' specified no date.", web.SessionId, web.ClientId, contract.Symbol) );

				auto pResults = make_unique<Proto::Results::OptionValues>(); pResults->set_id( params.id() );
				sp<Proto::Results::ExchangeContracts> pOptionParams;
				auto loadRight = [&]( SecurityRight right )
				{
					auto fetch = [&]( DayIndex expiration )noexcept(false)
					{
						var ibContract = TwsClientCache::ToContract( contract.Symbol, expiration, right, params.start_srike() && params.start_srike()==params.end_strike() ? params.start_srike() : 0 );
						auto pContracts = _sync.ReqContractDetails( ibContract ).get();
						if( pContracts->size()<1 )
							THROW( Exception("'{}' - '{}' {} has {} contracts", contract.Symbol, DateTime{Chrono::FromDays(expiration)}.DateDisplay(), ToString(right), pContracts->size()) );
						var start = params.start_srike(); var end = params.end_strike();
						if( !ibContract.strike && (start!=0 || end!=0) )//remove contracts outside bounds?
						{
							auto pContracts2 = make_shared<vector<ContractDetails>>();
							for( var& details : *pContracts )
							{
								if( (start==0 || start<=details.contract.strike) && (end==0 || end>=details.contract.strike) )
									pContracts2->push_back( details );
							}
							pContracts = pContracts2;
						}
						var valueDay = OptionData::LoadDiff( contract, *pContracts, *pResults );
						if( !valueDay )
						{
							flat_map<double,ContractPK> sorted;
							for( var& details : *pContracts )
								sorted.emplace( details.contract.strike, details.contract.conId );

							auto pExpiration = pResults->add_option_days();
							pExpiration->set_is_call( right==SecurityRight::Call );
							pExpiration->set_expiration_days( expiration );
							for( var& [strike,contractId] : sorted )
							{
								auto pValue = pExpiration->add_values();
								pValue->set_id( contractId );
								//DBG( "strike = '{}'"sv, details.contract.strike );
								pValue->set_strike( static_cast<float>(strike) );
							}
						}
						return valueDay;
					};
					if( params.start_expiration()==params.end_expiration() )
						pResults->set_day( fetch(params.start_expiration()) );
					else
					{
						if( !pOptionParams )
							pOptionParams = _sync.ReqSecDefOptParamsSmart( underlyingId, contract.Symbol );
						var end = params.end_expiration() ? params.end_expiration() : std::numeric_limits<DayIndex>::max();
						for( var expiration : pOptionParams->expirations() )
						{
							if( expiration<params.start_expiration() || expiration>end )
								continue;
							try
							{
								pResults->set_day( fetch(expiration) );//sets day multiple times...
							}
							catch( const IBException& e )
							{
								if( e.ErrorCode!=200 )//No security definition has been found for the request
									throw e;
							}
						}
					}
				};
				if( (params.security_type() & SecurityRight::Call) )
					loadRight( SecurityRight::Call );
				if( (params.security_type() & SecurityRight::Put) )
					loadRight( SecurityRight::Put );

				if( pResults->option_days_size() )
				{
					auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_options( pResults.release() );
					web.Push( pUnion );
				}
				else
					web.WebSendPtr->Push( IBException{"No previous dates found", -2}, {web.SessionId, underlyingId} );
			}
			catch( const IBException& e ){ web.Push( e ); }
			catch( const Exception& e ){ web.Push( e ); }
		} ).detach();
	}

	void WebRequestWorker::RequestFundamentalData( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ClientKey& key )noexcept
	{
		std::thread( [current=PreviousTradingDay(), contractIds, web=ProcessArg{key,_pWebSend}]()
		{
			Threading::SetThreadDscrptn( "RatioReq" );
			for( var contractId : contractIds )
			{
				try
				{
					//var pDetails = _sync.ReqContractDetails( contractId ).get(); THROW_IF( pDetails->size()!=1, IBException(format("{} has {} contracts", contractId, pDetails->size()), -1) );
					var tick = TickManager::Ratios( contractId ).get();
					auto fundamentals = tick.Ratios();
					auto pRatios = make_unique<Proto::Results::Fundamentals>();
					pRatios->set_request_id( web.ClientId );
					for( var& [name,value] : fundamentals )
						(*pRatios->mutable_values())[name] = value;
					auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_fundamentals( pRatios.release() );
					web.Push( pUnion );
				}
				catch( const Exception& e )
				{
					web.Push( e );
					return;
				}
			}
			web.Push( EResults::MultiEnd );
		} ).detach();
	}
}