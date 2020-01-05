#include "WrapperWeb.h"
#include <thread>
#include <EReaderOSSignal.h>
#include "../../Framework/source/Settings.h"
#include "EWebReceive.h"
#include "EWebSend.h"
#include "WebSocket.h"
#include "../../MarketLibrary/source/TwsClient.h"
#include "../../MarketLibrary/source/types/Contract.h"
#include <TickAttrib.h>

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
#define _socket WebSocket::Instance()
	sp<WrapperWeb> WrapperWeb::_pInstance{nullptr};
	WrapperWeb::WrapperWeb()noexcept(false):
		_pReaderSignal{ make_shared<EReaderOSSignal>() }
	{}

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

	void WrapperWeb::managedAccounts( const std::string& accountsList )noexcept
	{
		WrapperLog::managedAccounts( accountsList );
		auto pAccountList = new Proto::Results::AccountList();
		var accounts = StringUtilities::Split( accountsList );
		for( var& account : accounts )
			pAccountList->add_numbers( account );

		_socket.PushAllocated( pAccountList );
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
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::PositionMultiEnd ); pValue->set_intvalue( reqId );
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
		pBar->set_time( time.TimeT() );

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
		_socket.PushAllocated( reqId, pData.release() );
		_historicalData.erase( reqId );
	}


	void WrapperWeb::error( int id, int errorCode, const std::string& errorString )noexcept
	{
		WrapperLog::error( id, errorCode, errorString );
		auto p = WebSocket::InstancePtr();
		if( p )
			p->AddError( id, errorCode, errorString );
	}

	void HandleBadTicker( TickerId ibReqId )
	{
		DBG( "Could not find session for ticker req:  '{}'.", ibReqId );
		TwsClient::Instance().cancelMktData( ibReqId );
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
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_tickprice( pPrice );
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
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_ticksize( pMessage );
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
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tickgeneric( pMessage );
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
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_tickstring( pMessage );
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
		Contract myContract{contract};
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