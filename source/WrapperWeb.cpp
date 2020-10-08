#include "WrapperWeb.h"
#include <thread>
#include <TickAttrib.h>
#include <CommissionReport.h>
#include <Execution.h>

#include "../../Framework/source/Settings.h"
#include "../../Framework/source/collections/UnorderedSet.h"
#include "EWebReceive.h"
#include "WebSocket.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../MarketLibrary/source/types/Contract.h"
#include "../../MarketLibrary/source/types/MyOrder.h"
#include "../../MarketLibrary/source/types/OrderEnums.h"
#define var const auto

namespace Jde::Markets::TwsWebSocket
{
#define _socket WebSocket::Instance()
#define _client TwsClientSync::Instance()
	using Proto::Results::EResults;
	sp<WrapperWeb> WrapperWeb::_pInstance{nullptr};
	WrapperWeb::WrapperWeb()noexcept(false):
		_accounts{ SettingsPtr->Map<string>("accounts") }
	{}

	void WrapperWeb::CreateInstance( const TwsConnectionSettings& /*settings*/ )noexcept
	{
		ASSERT( !_pInstance );
		_pInstance = sp<WrapperWeb>( new WrapperWeb() );
		_pInstance->CreateClient( SettingsPtr->Get<uint>("twsClientId") );
		//TwsClientSync::CreateInstance( settings, _pInstance, _pInstance->_pReaderSignal, SettingsPtr->Get<uint>("twsClientId") );
	}
	WrapperWeb& WrapperWeb::Instance()noexcept
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}

	void WrapperWeb::nextValidId( ::OrderId orderId)noexcept
	{
		WrapperLog::nextValidId( orderId );
		TwsClientSync::Instance().SetRequestId( orderId );
	}

	void WrapperWeb::orderStatus( ::OrderId orderId, const std::string& status, double filled,	double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )noexcept
	{
		WrapperLog::orderStatus( orderId, status, filled,	remaining, avgFillPrice, permId, parentId, lastFillPrice, clientId, whyHeld, mktCapPrice );
		auto p = new Proto::Results::OrderStatus{};
		p->set_order_id( orderId );
		p->set_status( ToOrderStatus(status) );
		p->set_filled( filled );
		p->set_remaining( remaining );
		p->set_average_fill_price( avgFillPrice );
		p->set_perm_id( permId );
		p->set_parent_id( parentId );
		p->set_last_fill_price( lastFillPrice );
		p->set_client_id( clientId );
		p->set_why_held( whyHeld );
		p->set_market_cap_price( mktCapPrice );
		_socket.Push( EResults::OrderStatus_, [p](MessageType& msg){msg.set_allocated_order_status(new Proto::Results::OrderStatus{*p});} );
		if( !_socket.PushAllocated(orderId, [p](MessageType& msg, ClientRequestId id){p->set_id( id ); msg.set_allocated_order_status( p );}) )
			delete p;
	}
	void WrapperWeb::openOrder( ::OrderId orderId, const ::Contract& contract, const ::Order& order, const ::OrderState& state )noexcept
	{
		WrapperLog::openOrder( orderId, contract, order, state );
		auto p = new Proto::Results::OpenOrder{};
		p->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		p->set_allocated_order( MyOrder{order}.ToProto(true).get() );
		p->set_allocated_state( MyOrder::ToAllocatedProto(state) );
		_socket.Push( EResults::OpenOrder_, [p](MessageType& msg){msg.set_allocated_open_order(new Proto::Results::OpenOrder{*p});} );
		if( !_socket.PushAllocated(orderId, [p](MessageType& msg, ClientRequestId id){p->set_web_id(id); msg.set_allocated_open_order( p );}) )
		{
			TRACE( "({}) openOrder request not found"sv, orderId );
			delete p;
		}
	}
	void WrapperWeb::openOrderEnd()noexcept
	{
		WrapperLog::openOrderEnd();
		_socket.Push( EResults::OpenOrderEnd, [](MessageType& msg){msg.set_type(EResults::OpenOrderEnd);} );
	}
	void WrapperWeb::positionMulti( int reqId, const std::string& account, const std::string& modelCode, const ::Contract& contract, double pos, double avgCost )noexcept
	{
		WrapperLog::positionMulti( reqId, account, modelCode, contract, pos, avgCost );
		auto pUpdate = new Proto::Results::PositionMulti();
		//pUpdate->set_request_id( reqId );
		pUpdate->set_account( account );
		pUpdate->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		pUpdate->set_position( pos );
		pUpdate->set_avgerage_cost( avgCost );
		pUpdate->set_model_code( modelCode );

		_socket.PushAllocated( reqId, [p=pUpdate](MessageType& msg, ClientRequestId id){p->set_id( id ); msg.set_allocated_position_multi( p );} );
	}
	void WrapperWeb::positionMultiEnd( int reqId )noexcept
	{
		WrapperLog::positionMultiEnd( reqId );
		_socket.Push( reqId, EResults::PositionMultiEnd );
	}

	void WrapperWeb::managedAccounts( const std::string& accountsList )noexcept
	{
		WrapperLog::managedAccounts( accountsList );
		Proto::Results::StringMap accountList;
		accountList.set_result( EResults::ManagedAccounts );
		var accounts = StringUtilities::Split( accountsList );
		for( var& account : accounts )
			(*accountList.mutable_values())[account] = _accounts.find(account)!=_accounts.end() ? _accounts.find(account)->second : account;

		_socket.Push( EResults::ManagedAccounts, [&accountList]( auto& type ){ type.set_allocated_string_map(new Proto::Results::StringMap{accountList}); });
	}
	void WrapperWeb::accountDownloadEnd( const std::string& accountName )noexcept
	{
		WrapperLog::accountDownloadEnd( accountName );
		_socket.PushAccountDownloadEnd( accountName );
	}
	void WrapperWeb::accountUpdateMulti( int reqId, const std::string& accountName, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept
	{
		WrapperLog::accountUpdateMulti( reqId, accountName, modelCode, key, value, currency );
		auto pUpdate = new Proto::Results::AccountUpdateMulti();
		pUpdate->set_request_id( reqId );
		pUpdate->set_account( accountName );
		pUpdate->set_key( key );
		pUpdate->set_value( value );
		pUpdate->set_currency( currency );
		pUpdate->set_model_code( modelCode );

		_socket.PushAllocated( pUpdate );
	}
	void WrapperWeb::accountUpdateMultiEnd( int reqId )noexcept
	{
		WrapperLog::accountUpdateMultiEnd( reqId );
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::PositionMultiEnd ); pValue->set_int_value( reqId );
		_socket.PushAllocated( reqId, pValue );
	}
	void WrapperWeb::historicalData( TickerId reqId, const ::Bar& bar )noexcept
	{
		if( Cache::TryGet<uint>("breakpoint.BGGSQ") && *Cache::TryGet<uint>("breakpoint.BGGSQ")==reqId )
			TRACE0( "Break here."sv );
		if( WrapperSync::historicalDataSync(reqId, bar) )
			return;
		unique_lock l{ _historicalDataMutex };
		auto& pData = _historicalData.emplace( reqId, make_unique<Proto::Results::HistoricalData>() ).first->second;
		auto pBar = pData->add_bars();
		WrapperCache::ToBar( bar, *pBar );
	}
	void WrapperWeb::historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )noexcept
	{
		if( WrapperSync::historicalDataEndSync(reqId, startDateStr, endDateStr) )
			return;

		unique_lock l{ _historicalDataMutex };
		auto& pData = _historicalData.emplace( reqId, make_unique<Proto::Results::HistoricalData>() ).first->second;
		_socket.PushAllocated( reqId, pData.release(), true );
		_historicalData.erase( reqId );
	}

	void WrapperWeb::error( int id, int errorCode, const std::string& errorString )noexcept
	{
		auto p = WebSocket::InstancePtr();
		if( !WrapperSync::error2(id, errorCode, errorString) && p )
		{
			if( errorCode==162 && p->HasHistoricalRequest(id) )// _historicalCrcs.Has(id)
				p->PushAllocated( id, new Proto::Results::HistoricalData{}, true );
			else
				p->PushError( id, errorCode, errorString );
		}
	}

	void WrapperWeb::HandleBadTicker( TickerId ibReqId )noexcept
	{
		if( _canceledItems.emplace(ibReqId) )
		{
			DBG( "Could not find session for ticker req:  '{}'."sv, ibReqId );
			TwsClientSync::Instance().cancelMktData( ibReqId );
		}
	}
	void WrapperWeb::tickPrice( TickerId ibReqId, TickType field, double price, const TickAttrib& attrib )noexcept
	{
		if( WrapperSync::TickPrice(ibReqId, field, price, attrib) )
			return;
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pAttributes = new Proto::Results::TickAttrib(); pAttributes->set_can_auto_execute( attrib.canAutoExecute ); pAttributes->set_past_limit( attrib.pastLimit ); pAttributes->set_pre_open( attrib.preOpen );
		auto pPrice = new Proto::Results::TickPrice(); pPrice->set_request_id( clientId ); pPrice->set_tick_type( (Proto::Results::ETickType)field ); pPrice->set_price( price ); pPrice->set_allocated_attributes( pAttributes );
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_tick_price( pPrice );
		_socket.Push( sessionId, pMessage );
	}
	void WrapperWeb::tickSize( TickerId ibReqId, TickType field, int size )noexcept
	{
		if( WrapperSync::TickSize(ibReqId, field, size) )
			return;
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickSize(); pMessage->set_request_id( clientId ); pMessage->set_tick_type( (Proto::Results::ETickType)field ); pMessage->set_size( size );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_size( pMessage );
		_socket.Push( sessionId, pUnion );
	}

	void WrapperWeb::tickGeneric( TickerId ibReqId, TickType field, double value )noexcept
	{
		if( WrapperSync::TickGeneric(ibReqId, field, value) )
			return;
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickGeneric(); pMessage->set_request_id( clientId ); pMessage->set_tick_type( (Proto::Results::ETickType)field ); pMessage->set_value( value );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_generic( pMessage );
		_socket.Push( sessionId, pUnion );
	}
	void WrapperWeb::tickString( TickerId ibReqId, TickType field, const std::string& value )noexcept
	{
		if( WrapperSync::TickString(ibReqId, field, value) )
			return;

		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickString(); pMessage->set_request_id( clientId ); pMessage->set_tick_type( (Proto::Results::ETickType)field ); pMessage->set_value( value );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_string( pMessage );
		_socket.Push( sessionId, pUnion );
	}

	void WrapperWeb::tickSnapshotEnd( int ibReqId )noexcept
	{
		WrapperLog::tickSnapshotEnd( ibReqId );
		_socket.Push( ibReqId, Proto::Results::EResults::TickSnapshotEnd );
	}

	void WrapperWeb::updateAccountValue(const std::string& key, const std::string& value, const std::string& currency, const std::string& accountName )noexcept
	{
		WrapperLog::updateAccountValue( key, value, currency, accountName );
		Proto::Results::AccountUpdate update;
		update.set_account( accountName );
		update.set_key( key );
		update.set_value( value );
		update.set_currency( currency );

		_socket.Push( update );
	}
	void WrapperWeb::updatePortfolio( const ::Contract& contract, double position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountNumber )noexcept
	{
		WrapperLog::updatePortfolio( contract, position, marketPrice, marketValue, averageCost, unrealizedPNL, realizedPNL, accountNumber );
		Proto::Results::PortfolioUpdate update;
		Contract myContract{ contract };
//		ContractPK underlyingId{0};
		update.set_allocated_contract( myContract.ToProto(true).get() );
		update.set_position( position );
		update.set_market_price( marketPrice );
		update.set_market_value( marketValue );
		update.set_average_cost( averageCost );
		update.set_unrealized_pnl( unrealizedPNL );
		update.set_realized_pnl( realizedPNL );
		update.set_account_number( accountNumber );
		if( myContract.SecType==SecurityType::Option )
		{
			const string cacheId{ format("reqContractDetails.{}", myContract.Symbol) };
			if( Cache::Has(cacheId) )
			{
				var details = Cache::Get<vector<::ContractDetails>>( cacheId );
				if( details->size()==1 )
					update.mutable_contract()->set_underlying_id( details->front().underConId );
				else
					WARN( "'{}' returned multiple securities"sv, myContract.Symbol );
			}
		}
		_socket.Push( update );
	}
	void WrapperWeb::updateAccountTime( const std::string& timeStamp )noexcept
	{
		WrapperLog::updateAccountTime( timeStamp );//not sure what to do about this, no reqId or accountName
	}

	void WrapperWeb::contractDetails( int reqId, const ::ContractDetails& contractDetails )noexcept
	{
		if( _detailsData.Contains(reqId) )
			WrapperSync::contractDetails( reqId, contractDetails );
		else
		{
			WrapperLog::contractDetails( reqId, contractDetails );
			_socket.PushAllocated( reqId, ToProto(contractDetails) );
		}
	}
	void WrapperWeb::contractDetailsEnd( int reqId )noexcept
	{
		if( _detailsData.Contains(reqId) )
			WrapperSync::contractDetailsEnd( reqId );
		else
		{
			WrapperLog::contractDetailsEnd( reqId );
			_socket.ContractDetailsEnd( reqId );
		}
	}

	void WrapperWeb::commissionReport( const CommissionReport& ib )noexcept
	{
		Proto::Results::CommissionReport a; a.set_exec_id( ib.execId ); a.set_commission( ib.commission ); a.set_currency( ib.currency ); a.set_realized_pnl( ib.realizedPNL ); a.set_yield( ib.yield ); a.set_yield_redemption_date( ib.yieldRedemptionDate );
		_socket.Push( a );
		//_socket.PushAllocated( orderId, [a](MessageType& msg, ClientRequestId id){ auto p = new Results::CommissionReport(a); p->set_id( id ); msg.set_allocated_commission_report( p );} );
	}
	void WrapperWeb::execDetails( int reqId, const ::Contract& contract, const Execution& ib )noexcept
	{
		auto p = new Proto::Results::Execution();
		p->set_exec_id( ib.execId );
		var time = ib.time;
		ASSERT( time.size()==18 );
		if( time.size()==18 )//"20200717  10:23:05"
			p->set_time( (uint32)DateTime( (uint16)stoi(time.substr(0,4)), (uint8)stoi(time.substr(4,2)), (uint8)stoi(time.substr(6,2)), (uint8)stoi(time.substr(10,2)), (uint8)stoi(time.substr(13,2)), (uint8)stoi(time.substr(16,2)) ).TimeT() );
		p->set_account_number( ib.acctNumber );
		p->set_exchange( ib.exchange );
		p->set_side( ib.side );
		p->set_shares( ib.shares );
		p->set_price( ib.price );
		p->set_perm_id( ib.permId );
		p->set_client_id( ib.clientId );
		p->set_order_id( ib.orderId );
		p->set_liquidation( ib.liquidation );
		p->set_cumulative_quantity( ib.cumQty );
		p->set_avg_price( ib.avgPrice );
		p->set_order_ref( ib.orderRef );
		p->set_ev_rule( ib.evRule );
		p->set_ev_multiplier( ib.evMultiplier );
		p->set_model_code( ib.modelCode );
		p->set_last_liquidity( ib.lastLiquidity );
		p->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		_socket.PushAllocated( reqId, [p](MessageType& msg, ClientRequestId id){ p->set_id( id ); msg.set_allocated_execution( p );} );

	}
	void WrapperWeb::execDetailsEnd( int reqId )noexcept
	{
		WrapperLog::execDetailsEnd( reqId );
		_socket.Push( reqId, EResults::ExecutionDataEnd );
	}

	void WrapperWeb::tickNews( int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData )noexcept
	{
		auto pUpdate = new Proto::Results::TickNews();
		pUpdate->set_time( static_cast<uint32>(timeStamp) );
		pUpdate->set_provider_code( providerCode );
		pUpdate->set_article_id( articleId );
		pUpdate->set_headline( headline );
		pUpdate->set_extra_data( extraData );

		_socket.PushAllocated( tickerId, [p=pUpdate](MessageType& msg, ClientRequestId id){p->set_id( id ); msg.set_allocated_tick_news(p);} );
	}

	void WrapperWeb::newsArticle( int requestId, int articleType, const std::string& articleText )noexcept
	{
		auto pUpdate = new Proto::Results::NewsArticle();
		pUpdate->set_is_text( articleType==0 );
		pUpdate->set_value( articleText );
		_socket.PushAllocated( requestId, [p=pUpdate](MessageType& msg, ClientRequestId id){p->set_id( id ); msg.set_allocated_news_article(p);} );
	}
	void WrapperWeb::historicalNews( int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline )noexcept
	{
		WrapperLog::historicalNews( requestId, time, providerCode, articleId, headline );
		unique_lock l{_newsMutex};
		auto pExisting = _allocatedNews.find( requestId );
		if( pExisting ==_allocatedNews.end() )
			pExisting = _allocatedNews.emplace( requestId, new Proto::Results::HistoricalNewsCollection() ).first;
		auto pNew = pExisting->second->add_values();
		if( time.size()==21 )
			pNew->set_time( (uint32)DateTime((uint16)stoi(time.substr(0,4)), (uint8)stoi(time.substr(5,2)), (uint8)stoi(time.substr(8,2)), (uint8)stoi(time.substr(11,2)), (uint8)stoi(time.substr(14,2)), (uint8)stoi(time.substr(17,2)) ).TimeT() );//missing tenth seconds.

		pNew->set_provider_code( providerCode );
		pNew->set_article_id( articleId );
		pNew->set_headline( headline );
	}

	void WrapperWeb::historicalNewsEnd( int requestId, bool hasMore )noexcept
	{
		WrapperLog::historicalNewsEnd( requestId, hasMore );
		Proto::Results::HistoricalNewsCollection* pCollection;
		{
			unique_lock l{_newsMutex};
			auto pExisting = _allocatedNews.find( requestId );
			if( pExisting==_allocatedNews.end() )
				pCollection = new Proto::Results::HistoricalNewsCollection();
			else
			{
				pCollection = pExisting->second;
				_allocatedNews.erase( pExisting );
			}
		}
		pCollection->set_has_more( hasMore );
		_socket.PushAllocated( requestId, [p=pCollection](MessageType& msg, ClientRequestId id){p->set_request_id( id ); msg.set_allocated_historical_news(p);} );
	}

	void WrapperWeb::newsProviders( const std::vector<NewsProvider>& providers, bool isCache )noexcept
	{
		if( !isCache )
			WrapperCache::newsProviders( providers );
		//_newsProviderData.End( make_shared<std::vector<NewsProvider>>(providers) );
			//DBG0( "no listeners for newsProviders."sv );
		auto pMap = new Proto::Results::StringMap();
		pMap->set_result( EResults::NewsProviders );
		for( var& provider : providers )
			(*pMap->mutable_values())[provider.providerCode] = provider.providerName;

		_socket.Push( EResults::NewsProviders, [&pMap]( auto& type ){ type.set_allocated_string_map( pMap ); } );
	}

	void WrapperWeb::securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept
	{
		var handled = WrapperSync::securityDefinitionOptionalParameterSync( reqId, exchange, underlyingConId, tradingClass, multiplier, expirations, strikes );
		if( !handled && exchange=="SMART" )
			_socket.PushAllocated( reqId, new Proto::Results::OptionParams{ToOptionParam(exchange, underlyingConId, tradingClass, multiplier, expirations, strikes)} );
	}

	void WrapperWeb::securityDefinitionOptionalParameterEnd( int reqId )noexcept
	{
		bool handled = WrapperSync::securityDefinitionOptionalParameterEndSync( reqId );
		if( !handled )
			_socket.Push( reqId, Proto::Results::EResults::SecurityDefinitionOptionParameterEnd );
	}
}