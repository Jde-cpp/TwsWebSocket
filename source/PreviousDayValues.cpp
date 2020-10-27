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
	void fnctn( const TwsWebSocket::ProcessArg& arg, const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds )noexcept(false)//, TwsWebSocket::SessionId sessionId, TwsWebSocket::ClientRequestId requestId
	{
		flat_map<DayIndex,Proto::Results::DaySummary*> bars;
		try
		{
			for( var contractId : contractIds )
			{
				sp<vector<ContractDetails>> pDetails;
				try
				{
					pDetails = _sync.ReqContractDetails( contractId ).get();
					THROW_IF( pDetails->size()!=1, IBException( format("ReqContractDetails( {} ) returned {}.", contractId, pDetails->size()), -1) );
				}
				catch( const IBException& e )
				{
					THROW( IBException( format("ReqContractDetails( {} ) returned {}.", contractId, e.what()), e.ErrorCode, e.RequestId) );
				}
				Contract contract{ pDetails->front() };
				var current = CurrentTradingDay( *contract.TradingHoursPtr );
				var isOption = contract.SecType==SecurityType::Option;
				var isPreMarket = IsPreMarket( contract.SecType );
				var count = isOption || (IsOpen( contract.SecType ) && !isPreMarket) ? 1u : 2u;
				for( auto day=current; bars.size()<count; day = PreviousTradingDay(day) )
				{
					auto pBar1 = new Proto::Results::DaySummary{}; pBar1->set_request_id( arg.ClientId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
					bars.emplace( day, pBar1 );
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
					auto set = [&bars, groupByDay]( const vector<::Bar>& ibBars, function<void(Proto::Results::DaySummary& summary, double close)> fnctn )
					{
						var dayBars = groupByDay( ibBars );
						for( var& [day, pBar] : bars )
						{
							var pIbBar = dayBars.find( day );
							if( pIbBar!=dayBars.end() )
								fnctn( *pBar, pIbBar->second.back().close );
						}
					};
					var barSize = isOption ? Proto::Requests::BarSize::Hour : Proto::Requests::BarSize::Day;
					//var endDate = fillInBack ? current : PreviousTradingDay( current );
					var previous = endDate<current;
					var dayCount = !useRth ? 1 : useRth && previous ? std::max( 1u, count-1 ) : count;
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
					var dayBars = groupByDay( *pTrades );
					vector<DayIndex> sentDays{}; sentDays.reserve( bars.size() );
					for( var& [day, pBar] : bars )
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
						if( arg.SessionPK )
							arg.WebSendPtr->Push( pUnion, arg.SessionPK );
					}
					for( var day : sentDays )
						bars.erase( day );
				};
				var useRth = count==1;
				load( useRth, current );
				if( !useRth )
					load( !useRth, PreviousTradingDay(current) );
				std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
				bars.clear();
			}
			if( arg.SessionPK )
				arg.WebSendPtr->Push( EResults::MultiEnd, arg );
		}
		catch( const IBException& e )
		{
			std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
			if( arg.SessionPK )
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
		std::thread( [arg, contractIds](){ Threading::SetThreadDscrptn( format("{}-PrevDayValues", contractIds.size()==1 ? contractIds[0] : contractIds.size()) ); TwsWebSocket::fnctn( arg, contractIds );} ).detach();
	}
}
