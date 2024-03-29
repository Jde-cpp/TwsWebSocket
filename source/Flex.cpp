﻿#include "Flex.h"
#include <jde/io/File.h>
#include <jde/io/tinyxml2.h>
#include <jde/markets/types/Contract.h>
#include "../../Framework/source/Settings.h"
#include "../../Framework/source/Cache.h"

#define var const auto
#define _socket WebSocket::Instance()

//using boost::property_tree::ptree;
namespace Jde::Markets::TwsWebSocket
{
	using CacheType=flat_map<uint16,Proto::Results::Flex>;
	shared_mutex _cacheMutex;
	constexpr sv CacheName{ "Flex::SendTrades" };
	atomic<uint32> DirectoryCrc{0};
	static const LogTag& _logLevel = Logging::TagLevel( "app-flex" );
	α ToTimeT( sv date )noexcept->uint32;
	struct FlexAwaitable : IAwait
	{
		FlexAwaitable()noexcept:_root{ Settings::Get<fs::path>("twsWebSocket/flexPath").value_or(IApplication::ApplicationDataFolder()/"flex") }{}
		std::variant<sp<CacheType>,sp<IException>> _result;
		fs::path _root;
		uint32_t _crc;
		α await_ready()noexcept->bool override
		{
			if( !fs::exists(_root) )
				_result = IOException{ _root, "path does not exist" }.Clone();
			else
			{
				ostringstream crcStream;
				for( var& entry : fs::directory_iterator(_root) )
				{
					if( entry.is_regular_file() )
					{
#ifdef _MSC_VER
						uint timet = entry.last_write_time().time_since_epoch().count()/std::nano::den;
						crcStream << entry.path().string() << ";" << timet;
#else
						uint timet = std::chrono::file_clock::to_sys( entry.last_write_time() ).time_since_epoch().count()/std::nano::den;
						crcStream << entry.path().string() << ";" << timet;// Clock::to_time_t( x );
#endif
					}
				}
				_crc = Calc32RunTime( crcStream.str() );
				if( _crc==DirectoryCrc )
					_result = Cache::Get<CacheType>( string{CacheName} );
			}
			return ( _result.index()==0 && get<0>(_result) ) || _result.index()==1;
		}
		α Load( HCoroutine h )noexcept->Task
		{
			var _ = ( co_await CoLockKey( string{CacheName}, false ) ).UP<CoLockGuard>(); //unique_lock l{ _cacheMutex };
			flat_map<uint32,Proto::Results::Trade> trades;
			flat_map<uint,Proto::Results::FlexOrder> orders;
			LOG( "Flex::Load( '{}' )"sv, _root.string() );
			try
			{
				for( var& entry : fs::directory_iterator(_root) )
				{
					var& path = entry.path();
					if( path.extension()!=".xml" )
						continue;
					LOG( "Flex::Load( '{}' )"sv, path.string() );
					var pContent = ( co_await IO::Read(path, false) ).SP<string>(); CONTINUE_IF( !pContent->size(), "{} is empty", path.string() );
					Xml::XMLDocument doc{ *pContent, false };
					vector<sv> rg{ Str::Split("FlexQueryResponse,FlexStatements,FlexStatement,Trades") };
					var pTrades = doc.Find( std::span<sv>{rg} );
					for( auto p = pTrades ? pTrades->FirstChildElement() : nullptr; p; p = p->NextSiblingElement() )
					{
	// <Order accountId="da5sf6fdsa" symbol="AAPL" conid="265598" tradeID=""          dateTime="20191011;151042" quantity="-42" tradePrice="236.79" ibCommission="-1.210863226" buySell="SELL" ibOrderID="287226395" ibExecID=""                        orderTime="20191011;151036" openDateTime="" orderType="LMT" isAPIOrder="" />
	// <Trade accountId="adsffdsa56" symbol="AAPL" conid="265598" tradeID="628622969" dateTime="20191011;151042" quantity="-42" tradePrice="236.79" ibCommission="-1.210863226" buySell="SELL" ibOrderID="287226395" ibExecID="0000e0d5.5da0fc13.01.01" orderTime="20191011;151036" openDateTime="" orderType="LMT" isAPIOrder="Y" />
						//var& data = v.second;
						auto setValues = [&e=*p]( auto& value )
						{
							value.set_account_number( string{e.Attr("accountId")} );
							Contract contract{ e.Attr<ContractPK>("conid"), e.Attr("symbol") };
							value.set_allocated_contract( contract.ToProto().release() );
							value.set_order_type( string{e.Attr("orderType")} );

							value.set_time( ToTimeT(e.Attr("dateTime")) );
							if( var orderTime{e.Attr("orderTime") }; orderTime.size() )
								value.set_order_time( ToTimeT(orderTime) );
							value.set_side( string{e.Attr("buySell")} );
							value.set_shares( e.Attr<double>("quantity") );
							value.set_price( e.Attr<double>("tradePrice") );
							value.set_commission( e.Attr<double>("ibCommission") );
						};
						var orderId = p->Attr<uint32>( "ibOrderID" ); CONTINUE_IF( !orderId, "no order id" );
						if( p->Name()=="Order" )
						{
							if( orders.find(orderId)!=orders.end() )
								continue;
							auto& value = orders.try_emplace( orderId ).first->second;
							value.set_order_id( orderId );
							setValues( value );
						}
						else
						{
							var tradeId = p->Attr<uint32>( "tradeID" );//ibExecID is null for expired options
							if( trades.find(tradeId)!=trades.end() )
								continue;
							auto& value = trades.emplace( tradeId, Proto::Results::Trade{} ).first->second;
							value.set_id( p->Attr<uint32>("tradeID") );
							value.set_is_api( p->Attr("isAPIOrder")=="Y" );
							value.set_order_id( orderId );
							value.set_exec_id( string{p->Attr("ibExecID")} );
							setValues( value );
						}
					}
				}
				auto pValues = make_shared<CacheType>();
				for( var& idConfirm : trades )
				{
					var& confirm = idConfirm.second;
					var day = Chrono::ToDays( Clock::from_time_t(confirm.time()) );
					auto& flex = pValues->try_emplace( day, Proto::Results::Flex{} ).first->second;
					*flex.add_trades() = confirm;
				}
				for( var& idOrder : orders )
				{
					var& order = idOrder.second;
					var day = Chrono::ToDays( Clock::from_time_t(order.time()) );
					auto& flex = pValues->try_emplace( day, Proto::Results::Flex{} ).first->second;
					*flex.add_orders() = order;
				}
				DirectoryCrc = _crc;
				LOG( "Flex::~Load()"sv );
				_pPromise->get_return_object().SetResult( Cache::Set<CacheType>(string{CacheName}, pValues) );
			}
			catch( IException& e )
			{
				_pPromise->get_return_object().SetResult( e.Move() );
			}
			h.resume();
		}
		α await_suspend( HCoroutine h )noexcept->void  override
		{
			IAwait::await_suspend( h );
			string d = Threading::GetThreadDescription();
			Threading::SetThreadDscrptn( "Flex" );
			Load( h );
			Threading::SetThreadDscrptn( d );
		}
		α await_resume()noexcept->AwaitResult override{ return _pPromise ? IAwait::await_resume() : _result.index()==0 ? AwaitResult{ static_pointer_cast<void>(get<0>(_result)) } : AwaitResult{ get<1>(_result) }; }
};

	α ToTimeT( sv date )noexcept->uint32 /*time_t*/
	{
		uint32 value{0};
		if( date.size()==15 )
		{
			var easternTime = DateTime( To<uint16>(date.substr(0,4)), To<uint8>(date.substr(4,2)), To<uint8>(date.substr(6,2)), To<uint8>(date.substr(9,2)), To<uint8>(date.substr(11,2)), To<uint8>(date.substr(13,2)) ).GetTimePoint();
			var utcTime = easternTime-Timezone::EasternTimezoneDifference( easternTime );
			value = (uint32)Clock::to_time_t( utcTime );
		}
		else
			ERR( "Could not parse date '{}'."sv, date );
		return value;
	}

	α Load()noexcept{ return FlexAwaitable{}; }
	α Flex::SendTrades( str /*accountNumber*/, TimePoint startTime, TimePoint endTime, ProcessArg web )noexcept->Task
	{
		var startDay = Chrono::ToDays( startTime );
		var endDay = Chrono::ToDays( endTime );
		sp<CacheType> pData;
		try
		{
			pData = ( co_await Load() ).SP<CacheType>();
		}
		catch( const IException& e )
		{
			web.Push( "Send trades failed", e );
		}
		shared_lock l{ _cacheMutex };
		auto pResults = new Proto::Results::Flex(); pResults->set_id( web.ClientId );
		for( auto day=startDay; day<=endDay; ++day )
		{
			auto pConfirms = pData->find( day );
			if( pConfirms==pData->end() )
				continue;
			auto& flex = pConfirms->second;
			for( int i=0; i< flex.orders_size(); ++i )
				*pResults->add_orders() = flex.orders( i );

			for( int i=0; i< flex.trades_size(); ++i )
				*pResults->add_trades() = flex.trades( i );
		}
		LOG( "({})Flex '{}'-'{}' orders='{}' trades='{}'"sv, web.SessionId, DateDisplay(startDay), DateDisplay(endDay), pResults->orders_size(), pResults->trades_size() );
		Proto::Results::MessageUnion msg; msg.set_allocated_flex( pResults );
		//Make _webSend global instance.tw
		web.Push( move(msg) );
	}
}