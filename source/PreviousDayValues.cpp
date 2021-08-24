#include "PreviousDayValues.h"
#include "WebSocket.h"
#include "WebRequestWorker.h"
#define _sync TwsClientSync::Instance()
#define _socket TwsWebSocket::WebSocket::Instance()
#define var const auto

namespace Jde::Markets
{
namespace TwsWebSocket
{
	α Previous( int32 contractId, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task2
	{
		//TODO wrap in Try.
		var pContract = ( co_await TwsClientCo::ContractDetails( contractId ) ).Get<Contract>(); var& contract = *pContract;
		var current = CurrentTradingDay( contract );
		var isOption = contract.SecType==SecurityType::Option;
		var isPreMarket = IsPreMarket( contract.SecType );
		auto pBars = make_shared<flat_map<DayIndex,up<Proto::Results::DaySummary>>>();
		var count = (isOption && IsOpen(SecurityType::Stock)) || (IsOpen(contract.SecType) && !isPreMarket) ? 1u : 2u;
		for( auto day=current; pBars->size()<count; day = PreviousTradingDay(day) )
		{
			auto pBar1 = make_unique<Proto::Results::DaySummary>(); pBar1->set_request_id( arg.ClientId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
			pBars->emplace( day, move(pBar1) );
		}
		auto load = [isOption,count,current,arg,contract,pBars,pContract]( bool useRth, DayIndex endDate )->AWrapper
		{
			return AWrapper( [=]( HCoroutine h )->Task2
			{
				auto groupByDay = []( const vector<::Bar>& ibBars )->map<DayIndex,vector<::Bar>>
				{
					map<DayIndex,vector<::Bar>> dayBars;
					for( var& bar : ibBars )
					{
						var day = Chrono::DaysSinceEpoch( Clock::from_time_t(ConvertIBDate(bar.time)) );
						//DBG( "vol='{}' time='{}", bar.volume, ToIsoString(Clock::from_time_t(ConvertIBDate(bar.time))) );
						dayBars.try_emplace( day ).first->second.push_back( bar );
					}
					return dayBars;
				};
				auto set = [pBars, groupByDay]( const vector<::Bar>& ibBars, function<void(Proto::Results::DaySummary& summary, double close)> fnctn2 )
				{
					var dayBars = groupByDay( ibBars );
					for( var& x : *pBars )
					{
						var day2 = x.first;
						var& pBar = x.second;
						var pIbBar = dayBars.find( day2 );
						if( pIbBar!=dayBars.end() )
							fnctn2( *pBar, pIbBar->second.back().close );
					}
				};
				var barSize = isOption ? Proto::Requests::BarSize::Hour : Proto::Requests::BarSize::Day;
				var previous = endDate<current;
				var dayCount = !useRth ? 1 : useRth && previous ? std::max( (unsigned long)1u, (unsigned long)(count-1) ) : count;
				if( !IsOpen(contract) )
				{
					//DBG( "Calling asks..."sv );
					var pAsks = ( co_await TwsClientCo::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Ask, useRth) ).Get<vector<::Bar>>();
					//DBG( "Asks returned..."sv );
					set( *pAsks, []( Proto::Results::DaySummary& summary, double close ){ summary.set_ask( close ); } );

					var pBids = ( co_await TwsClientCo::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Bid, useRth) ).Get<vector<::Bar>>();
					set( *pBids, []( Proto::Results::DaySummary& summary, double close ){ summary.set_bid( close ); } );
				}
				//var setHighLowRth = !useRth && !previous;//after close & before open, want range/volume to be rth.

				var pTrades = ( co_await TwsClientCo::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, useRth) ).Get<vector<::Bar>>();
				auto trades = groupByDay( *pTrades );//options are by hour
				//var pRthTrades = useRth ? pTrades : ( co_await TwsClientCo::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true) ).Get<vector<::Bar>>();
				bool loaded;
				for( auto pDayBar = pBars->begin(); pDayBar!=pBars->end(); pDayBar = loaded ? pBars->erase(pDayBar) : std::next(pDayBar) )
				{
					var day = pDayBar->first; auto pIbBar = trades.find( day );
					if( !(loaded = pIbBar!=trades.end()) )
						continue;
					auto pBar = move( pDayBar->second ); auto ibTrades = pIbBar->second;
					pBar->set_count( static_cast<uint32>(ibTrades.size()) );
					pBar->set_open( ibTrades.front().open );
					pBar->set_close( ibTrades.back().close );
					if( !previous )
					{
						if( !useRth )
						{
							var pRth = ( co_await TwsClientCo::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true) ).Get<vector<::Bar>>();
							var trades2 = groupByDay( *pRth );
							if( auto pIbBar2 = trades2.find(day); pIbBar2!=trades2.end() )
								ibTrades = pIbBar2->second;
							DBG( "endDate={}, open={}, close={}"sv, endDate, ibTrades.front().open, ibTrades.back().close );
							pBar->set_open( ibTrades.front().open );
							pBar->set_close( ibTrades.back().close );
						}
						double high=0.0, low = std::numeric_limits<double>::max();
						for( var& bar : ibTrades )
						{
							pBar->set_volume( pBar->volume()+bar.volume );
							high = std::max( bar.high, high );
							low = std::min( bar.low, low );
						}
						pBar->set_high( high );
						pBar->set_low( low==std::numeric_limits<double>::max() ? 0 : low );
					}
					Proto::Results::MessageUnion msg; msg.set_allocated_day_summary( pBar.release() );
					if( arg.SessionId )
						arg.WebSendPtr->Push( move(msg), arg.SessionId );
				}
				//LOG( CoroutinePool::LogLevel(), "({}) - resume"sv, h.address() );
				h.resume();
			});
		};
		var useRth = count==1;
		co_await load( useRth, current );
		if( !useRth )
			co_await load( !useRth, PreviousTradingDay(current) );
	}
	void fnctn( const TwsWebSocket::ProcessArg& arg, const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds )noexcept(false)//, TwsWebSocket::SessionId sessionId, TwsWebSocket::ClientPK requestId
	{
		try
		{
			for( var contractId : contractIds )
			{
				sp<vector<ContractDetails>> pDetails;
				try
				{
					pDetails = _sync.ReqContractDetails( contractId ).get(); THROW_IF( pDetails->size()!=1, IBException( format("ReqContractDetails( {} ) returned {}.", contractId, pDetails->size()), -1) );
				}
				catch( const IBException& e )
				{
					THROW( IBException( format("ReqContractDetails( {} ) returned {}.", contractId, e.what()), e.ErrorCode, e.RequestId) );
				}
				Contract contract{ pDetails->front() };
				var current = CurrentTradingDay( contract );
				var isOption = contract.SecType==SecurityType::Option;
				var isPreMarket = IsPreMarket( contract.SecType );
				flat_map<DayIndex,unique_ptr<Proto::Results::DaySummary>> bars;
				var count = (isOption && IsOpen(SecurityType::Stock)) || (IsOpen(contract.SecType) && !isPreMarket) ? 1u : 2u;
				for( auto day=current; bars.size()<count; day = PreviousTradingDay(day) )
				{
					auto pBar1 = make_unique<Proto::Results::DaySummary>(); pBar1->set_request_id( arg.ClientId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
					bars.emplace( day, move(pBar1) );
				}
				auto load = [isPreMarket,isOption,count,current,&arg,contract,&bars]( bool useRth, DayIndex endDate )
				{
					auto groupByDay = []( const vector<::Bar>& ibBars )->map<DayIndex,vector<::Bar>>
					{
						map<DayIndex,vector<::Bar>> dayBars;
						for( var& bar : ibBars )
						{
							var day = Chrono::DaysSinceEpoch( Clock::from_time_t(ConvertIBDate(bar.time)) );
							//DBG( "vol='{}' time='{}", bar.volume, ToIsoString(Clock::from_time_t(ConvertIBDate(bar.time))) );
							dayBars.try_emplace( day ).first->second.push_back( bar );
						}
						return dayBars;
					};
					auto set = [&bars, groupByDay]( const vector<::Bar>& ibBars, function<void(Proto::Results::DaySummary& summary, double close)> fnctn2 )
					{
						var dayBars = groupByDay( ibBars );
						for( var& x : bars )
						{
							var day2 = x.first;
							var& pBar = x.second;

							//if( !pBar )
							//	continue;
							var pIbBar = dayBars.find( day2 );
							if( pIbBar!=dayBars.end() )
							{
								//var& bar = pIbBar->second.back();
						//		DBG( "open={}, volume={}, close={}"sv, bar.open, bar.volume, bar.close );
								fnctn2( *pBar, pIbBar->second.back().close );
							}
						}
					};
					var barSize = isOption ? Proto::Requests::BarSize::Hour : Proto::Requests::BarSize::Day;
					//var endDate = fillInBack ? current : PreviousTradingDay( current );
					var previous = endDate<current;
					var dayCount = !useRth ? 1 : useRth && previous ? std::max( (unsigned long)1u, (unsigned long)(count-1) ) : count;
					if( !IsOpen(contract) )
					{
						var pAsks = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Ask, useRth, true ).get();
						set( *pAsks, []( Proto::Results::DaySummary& summary, double close ){ summary.set_ask( close ); } );

						var pBids = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Bid, useRth, true ).get();
						set( *pBids, []( Proto::Results::DaySummary& summary, double close ){ summary.set_bid( close ); } );
					}
					if( isPreMarket && !previous )
					{
					/* rth in premarket for today makes no sense
						var today = current;
						var pToday = _sync.ReqHistoricalDataSync( contract, today, 1, barSize, TwsDisplay::Enum::Trades, false, true ).get();
						if( auto pDayBar = bars.find(today); pToday->size()==1 && pDayBar!=bars.end() )
						{
							if( sessionId )
							{
								var& bar = pToday->front();
								pDayBar->second->set_high( bar.high );
								pDayBar->second->set_low( bar.low );
								pDayBar->second->set_open( bar.open );
								auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_day_summary( pDayBar->second );
								_socket.Push( sessionId, pUnion );
							}
							bars.erase( today );
							return;
						}*/
					}
					var setHighLowRth = !useRth && !previous;//after close & before open, want range/volume to be rth.
				/* end date is screwed up.
					if( setHighLowRth )
					{
						var pTrades = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true, true ).get();
						var dayBars = groupByDay( *pTrades );
						for( var& [day, pBar] : bars )
						{
							var pIbBar = dayBars.find( day );
							if( pIbBar==dayBars.end() )
								continue;
							var& back = pIbBar->second.back();

							pBar->set_high( back.high );
							pBar->set_low( back.low );
							pBar->set_volume( back.volume );
						}
					}*/
					var pTrades = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, useRth, true ).get();
					//if( !arg.SessionId ) return;
					var dayBars = groupByDay( *pTrades );
					for( auto pDayBar = bars.begin(); pDayBar!=bars.end(); pDayBar = bars.erase(pDayBar) )
					{
						var day = pDayBar->first; auto& pBar = pDayBar->second;
						if( var pIbBar = dayBars.find( day ); pIbBar!=dayBars.end() )
						{
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
						}
						Proto::Results::MessageUnion msg; msg.set_allocated_day_summary( pBar.release() );
						if( arg.SessionId )
							arg.WebSendPtr->Push( move(msg), arg.SessionId );
					}
				};
				var useRth = count==1;
				load( useRth, current );
				if( !useRth )
					load( !useRth, PreviousTradingDay(current) );
			}
			if( arg.SessionId )
				arg.WebSendPtr->Push( EResults::MultiEnd, arg );
		}
		catch( const IBException& e )
		{
			//std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
			if( arg.SessionId )
				arg.WebSendPtr->Push( e, arg );
			else
				throw e;
		}
	}
}
	void TwsWebSocket::PreviousDayValues( ContractPK contractId )noexcept( false )//loads into cache for later
	{
		google::protobuf::RepeatedField<google::protobuf::int32> contractIds;
		contractIds.Add( contractId );
		TwsWebSocket::fnctn( {}, contractIds );
	}

	void TwsWebSocket::PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ProcessArg& arg )noexcept
	{
		for( var id : contractIds )
			Previous( id, arg );
		// std::thread( [arg, contractIds]()
		// {
		// 	Threading::SetThreadDscrptn( format("{}-PrevDayValues", contractIds.size()==1 ? contractIds[0] : contractIds.size()) );
		// 	TRY( TwsWebSocket::fnctn(arg, contractIds) );
		// }).detach();
	}
}
