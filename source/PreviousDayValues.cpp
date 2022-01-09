#include "PreviousDayValues.h"
//#include "WebSocket.h"
#include "WebRequestWorker.h"
#include "../../Framework/source/math/MathUtilities.h"
#include "../../MarketLibrary/source/types/Exchanges.h"
#define _sync TwsClientSync::Instance()
#define _socket TwsWebSocket::WebSocket::Instance()
#define var const auto

namespace Jde::Markets
{
namespace TwsWebSocket
{
	α Previous( int32 contractId, Markets::TwsWebSocket::ProcessArg arg, bool sendMultiEnd )noexcept->Coroutine::Task
	{
		//TODO wrap in Try.
		sp<::ContractDetails> pDetails;
		try
		{
			pDetails = ( co_await Tws::ContractDetail(contractId) ).SP<::ContractDetails>();
		}
		catch( IException& e )
		{
			co_return arg.Push( format("Could not load contract {}", contractId), e );
		}
		auto pContract = ms<Contract>( *pDetails );
		var current = CurrentTradingDay( *pContract );
		var isOption = pContract->SecType==SecurityType::Option;
		var isPreMarket = IsPreMarket( pContract->SecType );
		auto pBars = make_shared<flat_map<DayIndex,up<Proto::Results::DaySummary>>>();
		var count = (isOption && IsOpen(SecurityType::Stock)) || (IsOpen(pContract->SecType) && !isPreMarket) ? 1u : 2u;
		for( auto day=current; pBars->size()<count; day = PreviousTradingDay(day) )
		{
			auto pBar1 = make_unique<Proto::Results::DaySummary>(); pBar1->set_request_id( arg.ClientId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
			pBars->emplace( day, move(pBar1) );
		}
		auto load = [isOption,count,current,arg,pContract,pBars,isPreMarket]( bool useRth, DayIndex endDate, bool isEnd )->AWrapper
		{
			return AWrapper( [=]( HCoroutine h )->Task
			{
				try
				{
					auto groupByDay = []( const vector<::Bar>& ibBars )->map<DayIndex,vector<::Bar>>
					{
						map<DayIndex,vector<::Bar>> dayBars;
						for( var& bar : ibBars )
						{
							var day = Chrono::ToDays( Clock::from_time_t(ConvertIBDate(bar.time)) );
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
#define FETCH(x) ( co_await Tws::HistoricalData(pContract, endDate, dayCount, barSize, x, useRth) ).SP<vector<::Bar>>()
					if( !IsOpen(*pContract) && (!previous || !isOption) )//don't need bid/ask for previous day (at least options, not sure why for stocks.).
					{
						var pAsks = FETCH( TwsDisplay::Enum::Ask );
						set( *pAsks, []( Proto::Results::DaySummary& summary, double close ){ summary.set_ask( close ); } );

						var pBids = FETCH( TwsDisplay::Enum::Bid );
						set( *pBids, []( Proto::Results::DaySummary& summary, double close ){ summary.set_bid( close ); } );
					}
					map<DayIndex,vector<::Bar>> trades;
					if( !previous || !isOption )
					{
						var pTrades = FETCH( TwsDisplay::Enum::Trades );
						trades = groupByDay( *pTrades );//options are by hour
					}
					bool loaded;
					for( auto pDayBar = pBars->begin(); pDayBar!=pBars->end(); pDayBar = loaded ? pBars->erase(pDayBar) : std::next(pDayBar) )
					{
						var day = pDayBar->first; auto pIbBar = trades.find( day );
						if( loaded = pIbBar!=trades.end(), !loaded )
							continue;
						auto pBar = move( pDayBar->second ); auto ibTrades = pIbBar->second;
						pBar->set_count( static_cast<uint32>(ibTrades.size()) );
						pBar->set_open( ibTrades.front().open );
						pBar->set_close( ibTrades.back().close );
						if( !previous )
						{
							if( !useRth && !isPreMarket )
							{
								var pRth = ( co_await Tws::HistoricalData(pContract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, true) ).SP<vector<::Bar>>();
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
								pBar->set_volume( Round(ToDouble(pBar->volume())+ToDouble(bar.volume)) );
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
					if( isEnd )
						arg.Push( EResults::MultiEnd );
				}
				catch( IException& e )
				{
					h.promise().get_return_object().SetResult( e.Clone() );
				}
				h.resume();
			});
		};
		var useRth = count==1;
		co_await load( useRth, current, useRth && sendMultiEnd );
		if( !useRth )
			co_await load( !useRth, PreviousTradingDay(current), sendMultiEnd );
	}
}
	α TwsWebSocket::PreviousDayValues( ContractPK contractId )noexcept( false )->void//loads into cache for later
	{
		google::protobuf::RepeatedField<google::protobuf::int32> contractIds;
		contractIds.Add( contractId );
		Previous( contractId, {}, false );
	}

	α TwsWebSocket::PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ProcessArg& arg )noexcept->void
	{
		uint i=0;
		for( var id : contractIds )
			Previous( id, arg, ++i==contractIds.size() );
	}
}
