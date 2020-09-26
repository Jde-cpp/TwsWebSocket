#include "PreviousDayValues.h"
#include "WebSocket.h"

#define _sync TwsClientSync::Instance()
#define _socket TwsWebSocket::WebSocket::Instance()
#define var const auto

namespace Jde::Markets
{
	void fnctn( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, TwsWebSocket::SessionId sessionId, TwsWebSocket::ClientRequestId requestId )noexcept(false)
	{
		var current = CurrentTradingDay();
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
				var isOption = contract.SecType==SecurityType::Option;
				if( contract.Symbol=="AAPL" )
					DBG( "{} conId='{}'"sv, contract.Symbol, contract.Id );
				var isPreMarket = IsPreMarket( contract.SecType );
				var count = IsOpen( contract.SecType ) && !isPreMarket ? 1 : 2;
				for( auto day=current; bars.size()<count; day = PreviousTradingDay(day) )
				{
					auto pBar1 = new Proto::Results::DaySummary{}; pBar1->set_request_id( requestId ); pBar1->set_contract_id( contractId ); pBar1->set_day( day );
					bars.emplace( day, pBar1 );
				}
				auto load = [isPreMarket,isOption,count,current,sessionId,contract,&bars]( bool useRth, bool fillInBack )
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
						for( var& [day, pBar] : bars )
						{
							var pIbBar = dayBars.find( day );
							if( pIbBar!=dayBars.end() )
								fnctn( *pBar, pIbBar->second.back().close );
						}
					};
					var barSize = isOption ? Proto::Requests::BarSize::Hour : Proto::Requests::BarSize::Day;
					var endDate = fillInBack ? current : PreviousTradingDay( current );
					var dayCount = !useRth ? 1 : useRth && !fillInBack ? std::max( 1, count-1 ) : count;
					if( !IsOpen(contract) )
					{
						var pAsks = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Ask, useRth, true ).get();
						set( *pAsks, []( Proto::Results::DaySummary& summary, double close ){ summary.set_ask( close ); } );

						var pBids = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Bid, useRth, true ).get();
						set( *pBids, []( Proto::Results::DaySummary& summary, double close ){ summary.set_bid( close ); } );
					}
					if( isPreMarket && fillInBack )
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
					var setHighLowRth = !useRth && fillInBack;//after close & before open, want range/volume to be rth.
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
					//if( contract.Symbol=="AAPL" )
					//	DBG0( "AAPL"sv );
					var pTrades = _sync.ReqHistoricalDataSync( contract, endDate, dayCount, barSize, TwsDisplay::Enum::Trades, useRth, true ).get();
					if( !pTrades )
						DBG( "{} - !pTrades"sv, contract.Symbol );
					//ASSERT( pTrades );
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
						if( pBar->close()==9.16 || pBar->contract_id()==4964 )
							TRACE0("here"sv);
						auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_day_summary( pBar );
						sentDays.push_back( day );
						if( sessionId )
							_socket.Push( sessionId, pUnion );
					}
					for( var day : sentDays )
						bars.erase( day );
				};
				var useRth = isOption || count==1;
				load( useRth, true );
				if( !useRth )
					load( !useRth, false );
				std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
				bars.clear();
			}
			if( sessionId )
				_socket.Push( sessionId, requestId, Proto::Results::EResults::MultiEnd );
		}
		catch( const IBException& e )
		{
			std::for_each( bars.begin(), bars.end(), [](auto& bar){delete bar.second;} );
			if( sessionId )
				_socket.Push( sessionId, requestId, e );
			else
				throw e;
		}
	}
	void TwsWebSocket::PreviousDayValues( ContractPK contractId )noexcept( false )//loads into cache for later
	{
		google::protobuf::RepeatedField<google::protobuf::int32> contractIds;
		contractIds.Add( contractId );
		fnctn( contractIds, 0, 0 );
	}

	void TwsWebSocket::PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, SessionId sessionId, ClientRequestId requestId )noexcept
	{
		std::thread( [sessionId, contractIds, requestId](){ Threading::SetThreadDescription( format("{}-PrevDayValues", contractIds.size()==1 ? contractIds[0] : contractIds.size()) ); fnctn( contractIds, sessionId, requestId );} ).detach();
	}
}
