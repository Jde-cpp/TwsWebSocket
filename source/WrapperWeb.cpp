#include "WrapperWeb.h"
#include <CommissionReport.h>
#include <jde/markets/types/MyOrder.h>
#include "../../Framework/source/db/Database.h"
#include "../../Framework/source/db/GraphQL.h"
#include "../../Framework/source/db/Syntax.h"
#include "../../Framework/source/um/UM.h"
#include "../../MarketLibrary/source/types/OrderEnums.h"
#include "../../MarketLibrary/source/data/Accounts.h"
#include "WebCoSocket.h"

#define var const auto
#define _socket WebSocket::Instance()
#define _client TwsClientSync::Instance()
#define _webSend if( _pWebSend ) (*_pWebSend)

namespace Jde::Markets::TwsWebSocket
{
	using Proto::Results::EResults;
	using namespace Proto::Results;
	sp<WrapperWeb> WrapperWeb::_pInstance{nullptr};

	WrapperWeb::WrapperWeb()noexcept
	{}

	α WrapperWeb::CreateInstance()noexcept(false)->tuple<sp<TwsClientSync>,sp<WrapperWeb>>
	{
		ASSERT( !_pInstance );
		_pInstance = sp<WrapperWeb>( new WrapperWeb() );
		auto pClient = _pInstance->CreateClient( Settings::Getɛ<uint>("tws/clientId") );
		return make_tuple( pClient, _pInstance );
	}
	α WrapperWeb::Instance()noexcept->WrapperWeb&
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}
	α WrapperWeb::CreateClient( uint twsClientId )noexcept(false)->sp<TwsClientSync>
	{
		return WrapperSync::CreateClient( twsClientId );
	}

	α WrapperWeb::nextValidId( ::OrderId orderId)noexcept->void
	{
		WrapperLog::nextValidId( orderId );
		TwsClientSync::Instance().SetRequestId( orderId );
	}

	α WrapperWeb::orderStatus( ::OrderId orderId, str status, ::Decimal filled, ::Decimal remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, str whyHeld, double mktCapPrice )noexcept->void
	{
		WrapperLog::orderStatus( orderId, status, filled,	remaining, avgFillPrice, permId, parentId, lastFillPrice, clientId, whyHeld, mktCapPrice );
		if( !_pWebSend ) return;
		auto p = make_unique<Proto::OrderStatus>();
		p->set_order_id( orderId );
		p->set_status( ToOrderStatus(status) );
		p->set_filled( ToDouble(filled) );
		p->set_remaining( ToDouble(remaining) );
		p->set_average_fill_price( avgFillPrice );
		p->set_perm_id( permId );
		p->set_parent_id( parentId );
		p->set_last_fill_price( lastFillPrice );
		p->set_client_id( clientId );
		p->set_why_held( whyHeld );
		p->set_market_cap_price( mktCapPrice );
		try
		{
			_pWebSend->Push( orderId, [&p](MessageType& msg, ClientPK id){ p->set_id( id ); msg.set_allocated_order_status( new Proto::OrderStatus{*p});} );
		}
		catch( IException& ){}
		AllOpenOrdersAwait::Push( move(p) );
	}
	α WrapperWeb::openOrder( ::OrderId orderId, const ::Contract& contract, const ::Order& order, const ::OrderState& state )noexcept->void
	{
		AllOpenOrdersAwait::Push( *WrapperCo::OpenOrder(orderId, contract, order, state) );
	}
	α WrapperWeb::openOrderEnd()noexcept->void
	{
		WrapperLog::openOrderEnd();
		AllOpenOrdersAwait::Finish();
	}
	α WrapperWeb::positionMulti( int reqId, str account, str modelCode, const ::Contract& contract, ::Decimal pos, double avgCost )noexcept->void
	{
		WrapperLog::positionMulti( reqId, account, modelCode, contract, pos, avgCost );
		auto pUpdate = make_unique<Proto::Results::PositionMulti>();
		pUpdate->set_account( account );
		pUpdate->set_allocated_contract( Contract{contract}.ToProto().release() );
		pUpdate->set_position( ToDouble(pos) );
		pUpdate->set_avgerage_cost( avgCost );
		pUpdate->set_model_code( modelCode );

		_pWebSend->Push( reqId, [&p=pUpdate](MessageType& msg, ClientPK id){p->set_id( id ); msg.set_allocated_position_multi(p.release());} );
	}
	α WrapperWeb::positionMultiEnd( int reqId )noexcept->void
	{
		WrapperLog::positionMultiEnd( reqId );
		_pWebSend->Push( EResults::PositionMultiEnd, reqId );
	}

	α WrapperWeb::accountUpdateMulti( int reqId, str accountName, str modelCode, str key, str value, str currency )noexcept->void
	{
		WrapperLog::accountUpdateMulti( reqId, accountName, modelCode, key, value, currency );
		auto pUpdate = make_unique<Proto::Results::AccountUpdateMulti>();
		pUpdate->set_request_id( reqId );
		pUpdate->set_account( accountName );
		pUpdate->set_key( key );
		pUpdate->set_value( value );
		pUpdate->set_currency( currency );
		pUpdate->set_model_code( modelCode );

		_pWebSend->Push( reqId, [&p=pUpdate]( MessageType& m, ClientPK id )mutable
		{
			p->set_request_id(id);
			m.set_allocated_account_update_multi( p.release() );
		});
	}
	α WrapperWeb::accountUpdateMultiEnd( int reqId )noexcept->void
	{
		WrapperLog::accountUpdateMultiEnd( reqId );
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::PositionMultiEnd );// pValue->set_int_value( reqId );
		_pWebSend->Push( reqId, [pValue](MessageType& msg, ClientPK id){ pValue->set_int_value(id); msg.set_allocated_message(pValue); } );
	}
	α WrapperWeb::error( int reqId, int errorCode, str errorString, str /*advancedOrderRejectJson*/ )noexcept->void
	{
		if( !WrapperSync::error2(reqId, errorCode, errorString) )
		{
			if( errorCode==162 && _pWebSend->HasHistoricalRequest(reqId) )// _historicalCrcs.Has(id)
			{
				ASSERT(false);// not sure of use case _pWebSend->Push( reqId, [](MessageType& msg, ClientPK id){ auto p=new Proto::Results::HistoricalData{}; p->set_request_id(id); msg.set_allocated_historical_data(p); } );
			}
			//else if( reqId>0 )  Obsolete?
			//	_pWebSend->PushError( errorString, reqId, errorCode );
		}
	}

	α WrapperWeb::HandleBadTicker( TickerId reqId )noexcept->void
	{
		if( _canceledItems.emplace(reqId) )
		{
			DBG( "Could not find session for ticker req:  '{}'."sv, reqId );
			TwsClientSync::Instance().cancelMktData( reqId );
		}
	}
	α WrapperWeb::tickSnapshotEnd( int reqId )noexcept->void
	{
		WrapperLog::tickSnapshotEnd( reqId );
		_pWebSend->Push( Proto::Results::EResults::TickSnapshotEnd, reqId );
	}


	α WrapperWeb::updateAccountTime( str timeStamp )noexcept->void
	{
		WrapperLog::updateAccountTime( timeStamp );//not sure what to do about this, no reqId or accountName
	}

	α WrapperWeb::contractDetails( int reqId, const ::ContractDetails& contractDetails )noexcept->void
	{
		if( WrapperCo::_contractHandles.Has(reqId) )
			WrapperCo::contractDetails( reqId, contractDetails );
		else
		{
			WrapperLog::contractDetails( reqId, contractDetails );
			auto pReqDetails = _contractDetails.find( reqId );
			if( ; pReqDetails==_contractDetails.end() )
				pReqDetails = _contractDetails.emplace( reqId, make_unique<Proto::Results::ContractDetailsResult>() ).first;
			*pReqDetails->second->add_details() = ToProto( contractDetails );
		}
	}
	α WrapperWeb::contractDetailsEnd( int reqId )noexcept->void
	{
		if( WrapperCo::_contractHandles.Has(reqId) )
			WrapperCo::contractDetailsEnd( reqId );
		else
		{
			WrapperLog::contractDetailsEnd( reqId );
			var p = _contractDetails.find( reqId );
			var have = p !=_contractDetails.end();
			_pWebSend->ContractDetails( have ? std::move(p->second) : make_unique<Proto::Results::ContractDetailsResult>(), reqId );
		}
	}

	α WrapperWeb::commissionReport( const ::CommissionReport& ib )noexcept->void
	{
		Proto::Results::CommissionReport a; a.set_exec_id( ib.execId ); a.set_commission( ib.commission ); a.set_currency( ib.currency ); a.set_realized_pnl( ib.realizedPNL ); a.set_yield( ib.yield ); a.set_yield_redemption_date( ib.yieldRedemptionDate );
		_pWebSend->Push( a );
	}
	α WrapperWeb::execDetails( int reqId, const ::Contract& contract, const ::Execution& ib )noexcept->void
	{
		auto p = make_unique<Execution>();
		p->set_exec_id( ib.execId );
		var time = ib.time;
		ASSERT( time.size()==18 );
		if( time.size()==18 )//"20200717  10:23:05"
			p->set_time( (uint32)DateTime( (uint16)stoi(time.substr(0,4)), (uint8)stoi(time.substr(4,2)), (uint8)stoi(time.substr(6,2)), (uint8)stoi(time.substr(10,2)), (uint8)stoi(time.substr(13,2)), (uint8)stoi(time.substr(16,2)) ).TimeT() );
		p->set_account_number( ib.acctNumber );
		p->set_exchange( ib.exchange );
		p->set_side( ib.side );
		p->set_shares( ToDouble(ib.shares) );
		p->set_price( ib.price );
		p->set_perm_id( ib.permId );
		p->set_client_id( ib.clientId );
		p->set_order_id( ib.orderId );
		p->set_liquidation( ib.liquidation );
		p->set_cumulative_quantity( ToDouble(ib.cumQty) );
		p->set_avg_price( ib.avgPrice );
		p->set_order_ref( ib.orderRef );
		p->set_ev_rule( ib.evRule );
		p->set_ev_multiplier( ib.evMultiplier );
		p->set_model_code( ib.modelCode );
		p->set_last_liquidity( ib.lastLiquidity );
		p->set_allocated_contract( Contract{contract}.ToProto().release() );
		_pWebSend->Push( reqId, [&p](MessageType& msg, ClientPK id){ p->set_id( id ); msg.set_allocated_execution( p.release() );} );

	}
	α WrapperWeb::execDetailsEnd( int reqId )noexcept->void
	{
		WrapperLog::execDetailsEnd( reqId );
		_pWebSend->Push( EResults::ExecutionDataEnd, reqId );
	}
}