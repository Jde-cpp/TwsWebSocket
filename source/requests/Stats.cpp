#pragma once
#include "Stats.h"
#include <jde/markets/types/proto/ResultsMessage.h>
#include "../../../Framework/source/db/DataType.h"
#include "../../../Framework/source/db/Database.h"
#include "../../../MarketLibrary/source/data/StatAwait.h"

#define var const auto
namespace Jde::Markets::TwsWebSocket
{
	using namespace Proto;
	using namespace Proto::Requests;
	using namespace Proto::Results;
	α operator -(Stats s)ι->Stats{ return (Stats)-(int)s; }
	α ToString( Stats s )ι->sv
	{
		constexpr array<sv,7> values{ "LastUpdate", "ATH", "ATH_Day", "PWL", "YearLow", "YearHigh", "MA100" };
		return Str::FromEnum( values, s );
	}

	α RequestStats::Handle( up<Proto::Requests::RequestStats> pRequest, ProcessArg client )ι->Coroutine::Task
	{
		vector<DB::object> dbParams; dbParams.reserve( pRequest->contract_ids().size()+11 );
		try
		{
			flat_map<ContractPK,sp<Markets::Contract>> contracts;
			auto pDBContracts = ( co_await DB::SelectMap<ContractPK,optional<TimePoint>>( "select id, head_timestamp from ib_stock_contracts", "headTimestamps") ).SP<flat_map<ContractPK,optional<TimePoint>>>();
			for( var c : pRequest->contract_ids() )
			{
				auto p = ( co_await Tws::ContractDetail(c) ).SP<::ContractDetails>();
				auto pMyContract = ms<Markets::Contract>( *p );
				contracts.emplace( c, pMyContract );
				if( !pDBContracts->contains(c) )
				{
					co_await *DB::ExecuteCo( "insert into ib_stock_contracts values(?,?,?,?,?,?)", {(uint32_t)c, p->cusip.size() ? DB::object{p->cusip} : DB::object{}, DB::object{}, p->longName, (uint8_t)pMyContract->PrimaryExchange, p->contract.symbol} );
					pDBContracts->emplace( c, nullopt );
				}
				if( !(*pDBContracts)[c] )
				{
					(*pDBContracts)[c] = *( co_await Tws::HeadTimestamp(pMyContract->ToTws(), "TRADES") ).UP<TimePoint>();
					co_await *DB::ExecuteCo( "update ib_stock_contracts set head_timestamp=?", {*(*pDBContracts)[c]} );
				}
				dbParams.push_back( c );
			}
			
			CHECK( dbParams.size() );
			dbParams.push_back( (_int)Stats::UpdateDay );
			for( var s : pRequest->stats() )
			{
				dbParams.push_back( s );
				if( (Stats)s!=Stats::Pwl )
					dbParams.push_back( -s );
			}
			string sql = format( "select contract_id, statistics_id, value from mrk_statistic_values where contract_id in ({}) and statistics_id in ({}) order by abs(statistics_id)", DB::ToParamString(pRequest->contract_ids().size()), DB::ToParamString(dbParams.size()-pRequest->contract_ids().size()) );
			flat_set<ContractPK> recalc;
			auto pDBStats = ( co_await DB::SelectCo<ContractStats>( move(sql), dbParams, [&recalc, &contracts]( ContractStats& x, const DB::IRow& r )
			{
				var c = r.GetUInt32(0);
				var s = (Stats)r.GetInt32(1);
				double v = r.GetDouble( 2 );
				if( s==Stats::UpdateDay )
				{
					
					if( (DayIndex)v<PreviousTradingDay(*contracts[c]->TradingHoursPtr) )
						recalc.emplace( c );
					return;
				}
				if( ((Stats)abs((int)s)!=Stats::Pwl && (Stats)abs((int)s)!=Stats::Ath ) && recalc.contains(c) )
					return;
				auto p = x.add_stats();
				p->set_contract_id( c );
				p->set_stat( s );
				p->set_value( v );
			}) ).UP<ContractStats>();
			if( pDBStats->stats_size() )
				client.Push( Markets::ToMessage(new ContractStats(*pDBStats), client.ClientId) );
			for( var c : pRequest->contract_ids() )
			{
				auto requested = [&]( Stats s_ )->bool{ return std::find_if(pRequest->stats().begin(), pRequest->stats().end(), [=](var& s){return s==s_;})!=pRequest->stats().end(); };
				auto sentC = [&]( Stats s_, const ContractStats& coll )->bool{ return std::find_if(coll.stats().begin(), coll.stats().end(), [=](var& s){return s.contract_id()==c && s.stat()==s_;})!=coll.stats().end(); };
				auto pStats = mu<ContractStats>(); 
				auto sent = [&]( Stats s )->bool{ return sentC( s, *pDBStats) || sentC( s, *pStats); };
				if( requested(Stats::Pwl) && !sent(Stats::Pwl) )
				{
					auto pContract = ms<Markets::Contract>( *((co_await Tws::ContractDetail(c)).SP<::ContractDetails>()) );
					auto pBars = ( co_await Tws::HistoricalData(pContract, Chrono::ToDays((TimePoint)DateTime{2020,2,20}), 1, EBarSize::Day, Proto::Requests::Display::Trades, true) ).SP<vector<::Bar>>();
					double pwl{ pBars->size() ? (*pBars)[0].high : 0.0 };
					co_await *DB::ExecuteCo( format("insert into mrk_statistic_values values( ?, {}, ? );", (int)Proto::Stats::Pwl), {(uint32_t)c, pwl} );
					//std::this_thread::sleep_for( 1s );
					auto i=pStats->add_stats(); i->set_contract_id(c); i->set_stat(Stats::Pwl); i->set_value( pwl );
				}
				for( auto s_ : pRequest->stats() )
				{
					var s = (Stats)s_;
					if( sent(s) )
						continue;
					up<HistoricalDataCache::AthAwait::Result> pResult;
					try
					{
						pResult = ( co_await ReqAth(c, *(*pDBContracts)[c], s==Stats::Ath || s==Stats::Atl ? 0 : 365) ).UP<HistoricalDataCache::AthAwait::Result>();
					}
					catch( IException& e )
					{
						break;
					}
					auto add = [&]( Stats s )
					{
						if( !requested(s) )
							return;

						auto i=pStats->add_stats(); i->set_contract_id(c); i->set_stat(s); 
						if( s==Stats::MA100 )
							i->set_value( pResult->Average ); 
						else
						{
							var low = s==Stats::Atl || s==Stats::YearLow;
							if( low ) 
								i->set_value( pResult->Low ); 
							else
								i->set_value( pResult->High ); 
							auto d=pStats->add_stats(); d->set_contract_id(c); d->set_stat(-s); d->set_value( low ? pResult->LowDay : pResult->HighDay );
						}
					};
					add( s );
					if( s==Stats::YearLow ){ add( Stats::YearHigh ); add( Stats::MA100 ); }
					if( s==Stats::YearHigh ){ add( Stats::YearLow ); add( Stats::MA100 ); }
					if( s==Stats::MA100 ){ add( Stats::YearLow ); add( Stats::YearHigh ); }
					if( s==Stats::Ath ){ add( Stats::Atl ); }
					if( s==Stats::Atl ){ add( Stats::Ath ); }
				}
				if( pStats->stats().size() )
					client.Push( Markets::ToMessage(pStats.release(), client.ClientId) );
			}
			client.Push( Markets::ToMessage(new ContractStats(), client.ClientId) );//end
		}
		catch( IException& e )
		{
			client.Push( {}, move(e) );
		}
	}
}