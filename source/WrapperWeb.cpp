#include "WrapperWeb.h"
#include <thread>
#include <EReaderOSSignal.h>
#include "../../Framework/source/Settings.h"
#include "../../Framework/source/collections/UnorderedSet.h"
#include "EWebReceive.h"
#include "EWebSend.h"
#include "WebSocket.h"
#include "../../MarketLibrary/source/TwsClient.h"
#include "../../MarketLibrary/source/types/Contract.h"
#include "../../MarketLibrary/source/types/MyOrder.h"
#include "../../MarketLibrary/source/types/OrderEnums.h"
#include <TickAttrib.h>

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
#define _socket WebSocket::Instance()
	using Proto::Results::EResults;
	sp<WrapperWeb> WrapperWeb::_pInstance{nullptr};
	WrapperWeb::WrapperWeb()noexcept(false):
		_pReaderSignal{ make_shared<EReaderOSSignal>() },
		_accounts{ SettingsPtr->Map<string>("accounts") }
	{

	}

	void WrapperWeb::CreateInstance( const TwsConnectionSettings& settings )noexcept
	{
		ASSERT( !_pInstance );
		_pInstance = sp<WrapperWeb>( new WrapperWeb() );
		TwsClient::CreateInstance( settings, _pInstance, _pInstance->_pReaderSignal, SettingsPtr->Get<uint>("twsClientId") );
	}
	WrapperWeb& WrapperWeb::Instance()noexcept
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}

	void WrapperWeb::nextValidId( ibapi::OrderId orderId)noexcept
	{
		WrapperLog::nextValidId( orderId );
		TwsClient::Instance().SetRequestId( orderId );
	}

	void WrapperWeb::orderStatus( ibapi::OrderId orderId, const std::string& status, double filled,	double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )noexcept
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
	void WrapperWeb::openOrder( ibapi::OrderId orderId, const ibapi::Contract& contract, const ibapi::Order& order, const ibapi::OrderState& state )noexcept
	{
		WrapperLog::openOrder( orderId, contract, order, state );
		auto p = new Proto::Results::OpenOrder{};
		p->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		p->set_allocated_order( MyOrder{order}.ToProto(true).get() );
		p->set_allocated_state( MyOrder::ToAllocatedProto(state) );
		_socket.Push( EResults::OpenOrder_, [p](MessageType& msg){msg.set_allocated_open_order(new Proto::Results::OpenOrder{*p});} );
		if( !_socket.PushAllocated(orderId, [p](MessageType& msg, ClientRequestId id){p->set_web_id(id); msg.set_allocated_open_order( p );}) )
			delete p;
	}
	void WrapperWeb::openOrderEnd()noexcept
	{
		WrapperLog::openOrderEnd();
		_socket.Push( EResults::OpenOrderEnd, [](MessageType& msg){msg.set_type(EResults::OpenOrderEnd);} );
	}
	void WrapperWeb::managedAccounts( const std::string& accountsList )noexcept
	{
		WrapperLog::managedAccounts( accountsList );
		Proto::Results::AccountList accountList;
		var accounts = StringUtilities::Split( accountsList );
		for( var& account : accounts )
			(*accountList.mutable_values())[account] = _accounts.find(account)!=_accounts.end() ? _accounts.find(account)->second : account;

		_socket.Push( EResults::ManagedAccounts, [&accountList]( auto& type )
		{
			type.set_allocated_account_list( new Proto::Results::AccountList{accountList} );
		});
	}

	void WrapperWeb::accountUpdateMulti( int reqId, const std::string& accountName, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept
	{
		WrapperLog::accountUpdateMulti( reqId, accountName, modelCode, key, value, currency );
		auto pUpdate = new Proto::Results::AccountUpdateMulti();
		pUpdate->set_requestid( reqId );
		pUpdate->set_account( accountName );
		pUpdate->set_key( key );
		pUpdate->set_value( value );
		pUpdate->set_currency( currency );
		pUpdate->set_modelcode( modelCode );

		_socket.PushAllocated( pUpdate );
	}
	void WrapperWeb::accountUpdateMultiEnd( int reqId )noexcept
	{
		WrapperLog::accountUpdateMultiEnd( reqId );
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::PositionMultiEnd ); pValue->set_int_value( reqId );
		_socket.PushAllocated( reqId, pValue );
	}
	void WrapperWeb::historicalData( TickerId reqId, const ibapi::Bar& bar )noexcept
	{
		WrapperLog::historicalData( reqId, bar );
		unique_lock l{ _historicalDataMutex };
		auto& pData = _historicalData.emplace( reqId, make_unique<Proto::Results::HistoricalData>() ).first->second;
		auto pBar = pData->add_bars();
		l.unlock();
		var time = bar.time.size()==8 ? DateTime{ (uint16)stoi(bar.time.substr(0,4)), (uint8)stoi(bar.time.substr(4,2)), (uint8)stoi(bar.time.substr(6,2)) } : DateTime( stoi(bar.time) );
		pBar->set_time( (int)time.TimeT() );

		pBar->set_high( bar.high );
		pBar->set_low( bar.low );
		pBar->set_open( bar.open );
		pBar->set_close( bar.close );
		pBar->set_wap( bar.wap );
		pBar->set_volume( bar.volume );
		pBar->set_count( bar.count );
	}
	void WrapperWeb::historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )noexcept
	{
		WrapperLog::historicalDataEnd( reqId, startDateStr, endDateStr );
		unique_lock l{ _historicalDataMutex };
		auto& pData = _historicalData.emplace( reqId, make_unique<Proto::Results::HistoricalData>() ).first->second;
		_socket.PushAllocated( reqId, pData.release(), true );
		_historicalData.erase( reqId );
	}


	void WrapperWeb::error( int id, int errorCode, const std::string& errorString )noexcept
	{
		WrapperLog::error( id, errorCode, errorString );
		auto p = WebSocket::InstancePtr();
		if( p )
		{
			if( errorCode==162 && p->HasHistoricalRequest(id) )// _historicalCrcs.Has(id)
				p->PushAllocated( id, new Proto::Results::HistoricalData{}, true );
			else
				p->AddError( id, errorCode, errorString );
		}
	}

	void WrapperWeb::HandleBadTicker( TickerId ibReqId )noexcept
	{
		if( _canceledItems.emplace(ibReqId) )
		{
			DBG( "Could not find session for ticker req:  '{}'."sv, ibReqId );
			TwsClient::Instance().cancelMktData( ibReqId );
		}
	}
	void WrapperWeb::tickPrice( TickerId ibReqId, TickType field, double price, const TickAttrib& attrib )noexcept
	{
		WrapperLog::tickPrice( ibReqId, field, price, attrib );
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pAttributes = new Proto::Results::TickAttrib(); pAttributes->set_canautoexecute( attrib.canAutoExecute ); pAttributes->set_pastlimit( attrib.pastLimit ); pAttributes->set_preopen( attrib.preOpen );
		auto pPrice = new Proto::Results::TickPrice(); pPrice->set_requestid( clientId ); pPrice->set_ticktype( (Proto::Results::ETickType)field ); pPrice->set_price( price ); pPrice->set_allocated_attributes( pAttributes );
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_tick_price( pPrice );
		_socket.AddOutgoing( sessionId, pMessage );
	}
	void WrapperWeb::tickSize( TickerId ibReqId, TickType field, int size )noexcept
	{
		WrapperLog::tickSize( ibReqId, field, size );
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickSize(); pMessage->set_requestid( clientId ); pMessage->set_ticktype( (Proto::Results::ETickType)field ); pMessage->set_size( size );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_size( pMessage );
		_socket.AddOutgoing( sessionId, pUnion );
	}

	void WrapperWeb::tickGeneric( TickerId ibReqId, TickType field, double value )noexcept
	{
		WrapperLog::tickGeneric( ibReqId, field, value );
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickGeneric(); pMessage->set_requestid( clientId ); pMessage->set_ticktype( (Proto::Results::ETickType)field ); pMessage->set_value( value );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_generic( pMessage );
		_socket.AddOutgoing( sessionId, pUnion );
	}
	void WrapperWeb::tickString( TickerId ibReqId, TickType field, const std::string& value )noexcept
	{
		WrapperLog::tickString( ibReqId, field, value );
		var [sessionId,clientId] = _socket.GetClientRequest( ibReqId );
		if( !sessionId )
		{
			HandleBadTicker( ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::TickString(); pMessage->set_requestid( clientId ); pMessage->set_ticktype( (Proto::Results::ETickType)field ); pMessage->set_value( value );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tick_string( pMessage );
		_socket.AddOutgoing( sessionId, pUnion );
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
	void WrapperWeb::updatePortfolio( const ibapi::Contract& contract, double position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountNumber )noexcept
	{
		WrapperLog::updatePortfolio( contract, position, marketPrice, marketValue, averageCost, unrealizedPNL, realizedPNL, accountNumber );
		Proto::Results::PortfolioUpdate update;
		Contract myContract{ contract };
		update.set_allocated_contract( myContract.ToProto(true).get() );
		update.set_position( position );
		update.set_marketprice( marketPrice );
		update.set_marketvalue( marketValue );
		update.set_averagecost( averageCost );
		update.set_unrealizedpnl( unrealizedPNL );
		update.set_realizedpnl( realizedPNL );
		update.set_accountnumber( accountNumber );

		_socket.Push( update );
	}
	void WrapperWeb::updateAccountTime( const std::string& timeStamp )noexcept
	{
		WrapperLog::updateAccountTime( timeStamp );//not sure what to do about this, no reqId or accountName
	}

	void WrapperWeb::contractDetails( int reqId, const ibapi::ContractDetails& contractDetails )noexcept
	{
		WrapperLog::contractDetails( reqId, contractDetails );//not sure what to do about this, no reqId or accountName
		_socket.PushAllocated( reqId, ToProto(contractDetails) );
	}
	void WrapperWeb::contractDetailsEnd( int reqId )noexcept
	{
		WrapperLog::contractDetailsEnd( reqId );
		_socket.ContractDetailsEnd( reqId );
	}
}