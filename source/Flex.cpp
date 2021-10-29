#include "Flex.h"
#include <fstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "WebSocket.h"
#include <jde/markets/types/Contract.h>
#include "../../Framework/source/Settings.h"
#include "../../Framework/source/Cache.h"
#include <jde/io/File.h>

#define var const auto
#define _socket WebSocket::Instance()

using boost::property_tree::ptree;
namespace Jde::Markets::TwsWebSocket
{
	extern shared_ptr<Settings::Container> SettingsPtr;

	using CacheType=map<uint16,Proto::Results::Flex>;
	shared_mutex _cacheMutex;
	constexpr sv CacheName{ "Flex::SendTrades" };
	atomic<uint32> DirectoryCrc{0};

	α Load()noexcept(false)->sp<CacheType>;
	α Flex::SendTrades( str /*accountNumber*/, TimePoint startTime, TimePoint endTime, const ProcessArg& web )noexcept->void
	{
		try
		{
			var startDay = Chrono::DaysSinceEpoch( startTime );
			var endDay = Chrono::DaysSinceEpoch( endTime );
			auto pData = Load();

			shared_lock l{ _cacheMutex };
			auto pResults = new Proto::Results::Flex(); pResults->set_id( web.ClientId );
			std::ofstream os("/tmp/trades.csv");
			for( auto day=startDay; day<=endDay; ++day )
			{
				auto pConfirms = pData->find( day );
				if( pConfirms==pData->end() )
					continue;
				auto& flex = pConfirms->second;
				for( int i=0; i< flex.orders_size(); ++i )
					*pResults->add_orders() = flex.orders( i );

				for( int i=0; i< flex.trades_size(); ++i )
				{
					*pResults->add_trades() = flex.trades( i );
					os << flex.trades( i ).shares() << "," << flex.trades( i ).commission() << std::endl;
				}
			}
			DBG( "({})Flex '{}'-'{}' orders='{}' trades='{}'"sv, web.SessionId, Chrono::DateDisplay(startDay), Chrono::DateDisplay(endDay), pResults->orders_size(), pResults->trades_size() );
			Proto::Results::MessageUnion msg; msg.set_allocated_flex( pResults );
			//Make _webSend global instance.tw
			web.Push( move(msg) );
		}
		catch( const IException& e )
		{
			web.Push( e );
		}
	}
	α ToTimeT( str date )noexcept->time_t
	{
		time_t value{0};
		if( date.size()==15 )
		{
			var easternTime = DateTime( stoi(date.substr(0,4)), (uint8)stoi(date.substr(4,2)), (uint8)stoi(date.substr(6,2)), (uint8)stoi(date.substr(9,2)), (uint8)stoi(date.substr(11,2)), (uint8)stoi(date.substr(13,2)) ).GetTimePoint();
			var utcTime = easternTime-Timezone::EasternTimezoneDifference( easternTime );
			value = Clock::to_time_t( utcTime );
		}
		else
			ERR( "Could not parse date '{}'."sv, date );
		return value;
	}
	α Load()noexcept(false)->sp<CacheType>
	{
		unique_lock l{ _cacheMutex };
		var root = SettingsPtr->TryGet<fs::path>( "flexPath" ).value_or( IApplication::ApplicationDataFolder()/"flex" ); CHECK_PATH( root );
		var pEntries = IO::FileUtilities::GetDirectory( root );
		ostringstream crcStream;
		for( var& entry : *pEntries )
		{
			if( entry.is_regular_file() )
#ifdef _MSC_VER
				crcStream << entry.path().string() << ";"; //TODO:  << entry.last_write_time();
#else
				crcStream << entry.path().string() << ";" << fs::_FilesystemClock::to_time_t( entry.last_write_time() );
#endif
		}
		var crc = Calc32RunTime( crcStream.str() );
		if( crc==DirectoryCrc )
		{
			auto pCache = Cache::Get<CacheType>( string(CacheName) );
			if( pCache )
				return pCache;
		}
		map<string,Proto::Results::Trade> trades;
		map<uint,Proto::Results::FlexOrder> orders;
		DBG( "Flex::Load( '{}' )"sv, root.string() );
		try
		{
			for( var& entry : *pEntries )
			{
				var& path = entry.path();
				if( path.extension()!=".xml" )
					continue;
				var& strPath = path.string();
				TRACE( "Flex::read_xml( '{}' )"sv, strPath );
				ptree pt;
				read_xml( path.string(), pt );

  				for( ptree::value_type& v : pt.get_child("FlexQueryResponse.FlexStatements.FlexStatement.Trades") )
				{
// <Order accountId="da5sf6fdsa" symbol="AAPL" conid="265598" tradeID=""          dateTime="20191011;151042" quantity="-42" tradePrice="236.79" ibCommission="-1.210863226" buySell="SELL" ibOrderID="287226395" ibExecID=""                        orderTime="20191011;151036" openDateTime="" orderType="LMT" isAPIOrder="" />
// <Trade accountId="adsffdsa56" symbol="AAPL" conid="265598" tradeID="628622969" dateTime="20191011;151042" quantity="-42" tradePrice="236.79" ibCommission="-1.210863226" buySell="SELL" ibOrderID="287226395" ibExecID="0000e0d5.5da0fc13.01.01" orderTime="20191011;151036" openDateTime="" orderType="LMT" isAPIOrder="Y" />
					var isOrder = v.first=="Order";
					var& data = v.second;
					auto setValues = [&data]( auto& value )
					{
						value.set_account_number( data.get<string>("<xmlattr>.accountId") );
						Contract contract{ data.get<ContractPK>("<xmlattr>.conid"), data.get<string>("<xmlattr>.symbol") };
						value.set_allocated_contract( contract.ToProto(true).get() );
						var orderType = data.get_optional<string>( "<xmlattr>.orderType" );
						if( orderType.has_value() )
							value.set_order_type( orderType.value() );

						value.set_time( (uint32)ToTimeT(data.get<string>("<xmlattr>.dateTime")) );
						value.set_order_time( (uint32)ToTimeT(data.get<string>("<xmlattr>.orderTime")) );
						value.set_side( data.get<string>("<xmlattr>.buySell") );
						value.set_shares( data.get<double>("<xmlattr>.quantity") );
						value.set_price( data.get<double>("<xmlattr>.tradePrice") );
						value.set_commission( data.get<double>("<xmlattr>.ibCommission") );
					};
					var orderId = data.get<uint32>( "<xmlattr>.ibOrderID" );
					if( isOrder )
					{
						if( orders.find(orderId)!=orders.end() )
							continue;
						auto& value = orders.emplace( orderId, Proto::Results::FlexOrder{} ).first->second;
						value.set_order_id( orderId );
						setValues( value );
					}
					else
					{
						var execId = data.get<string>( "<xmlattr>.ibExecID" );
						if( trades.find(execId)!=trades.end() )
							continue;
						auto& value = trades.emplace( execId, Proto::Results::Trade{} ).first->second;
						value.set_id( data.get<uint32>("<xmlattr>.tradeID") );
						value.set_is_api( data.get<string>("<xmlattr>.isAPIOrder")=="Y" );
						value.set_order_id( orderId );
						value.set_exec_id( execId );
						setValues( value );
					}
				}
			}
		}
		catch( boost::exception& e )
		{
			THROW( "Could not parse flex files - '{}'", boost::diagnostic_information(&e) );
		}
		auto pValues = make_shared<CacheType>();
		for( var& idConfirm : trades )
		{
			var& confirm = idConfirm.second;
			var day = Chrono::DaysSinceEpoch( Clock::from_time_t(confirm.time()) );
			auto& flex = pValues->try_emplace( day, Proto::Results::Flex{} ).first->second;
			*flex.add_trades() = confirm;
		}
		for( var& idOrder : orders )
		{
			var& order = idOrder.second;
			var day = Chrono::DaysSinceEpoch( Clock::from_time_t(order.time()) );
			auto& flex = pValues->try_emplace( day, Proto::Results::Flex{} ).first->second;
			*flex.add_orders() = order;
		}
		DirectoryCrc = crc;
		return Cache::Set<CacheType>( string(CacheName), pValues );
	}
}