//x1 - User access.
//x2 - Global access.
//x3 - change user access.
//x4 - change global access.
//5 - fix mysql provider.

#include "WrapperWeb.h"
#include <thread>
#include <TickAttrib.h>
#include <CommissionReport.h>
#include <Execution.h>

#include "EWebReceive.h"
#include "WebCoSocket.h"
#include "../../Framework/source/um/UM.h"
#include "../../Framework/source/Settings.h"
#include "../../Framework/source/collections/Collections.h"
#include "../../Framework/source/db/Database.h"
#include "../../Framework/source/db/GraphQL.h"
#include "../../Framework/source/db/Syntax.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include <jde/markets/types/Contract.h>
#include <jde/markets/types/MyOrder.h>
#include "../../MarketLibrary/source/types/OrderEnums.h"

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	extern shared_ptr<Settings::Container> SettingsPtr;

#define _socket WebSocket::Instance()
#define _client TwsClientSync::Instance()
#define _webSend if( _pWebSend ) (*_pWebSend)

	using Proto::Results::EResults;
	sp<WrapperWeb> WrapperWeb::_pInstance{nullptr};

	WrapperWeb::WrapperWeb()noexcept
	{}

	tuple<sp<TwsClientSync>,sp<WrapperWeb>> WrapperWeb::CreateInstance()noexcept(false)
	{
		ASSERT( !_pInstance );
		_pInstance = sp<WrapperWeb>( new WrapperWeb() );
		sp<TwsClientSync> pClient = _pInstance->CreateClient( SettingsPtr->Get<uint>("twsClientId") );
		return make_tuple( pClient, _pInstance );
	}
	WrapperWeb& WrapperWeb::Instance()noexcept
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}
	sp<TwsClientSync> WrapperWeb::CreateClient( uint twsClientId )noexcept(false)
	{
		return WrapperSync::CreateClient( twsClientId );
	}

	bool WrapperWeb::TryTestAccess( UM::EAccess requested, sv name, SessionPK sessionId, bool allowDeleted )noexcept{auto p = _pInstance; return p ? p->tryTestAccess( requested, name, sessionId, allowDeleted ) : false; }
	bool WrapperWeb::tryTestAccess( UM::EAccess requested, sv name, SessionPK sessionId, bool allowDeleted )noexcept
	{
		var pSocket = WebCoSocket::Instance();
		return pSocket ? TryTestAccess( requested, pSocket->TryUserId(sessionId), name, allowDeleted ) : false;
	}
	bool WrapperWeb::TryTestAccess( UM::EAccess requested, UserPK userId, sv name, bool allowDeleted )noexcept{auto p = _pInstance; return p ? p->tryTestAccess( requested, userId, name, allowDeleted ) : false; }
	bool WrapperWeb::tryTestAccess( UM::EAccess requested, UserPK userId, sv name, bool allowDeleted )noexcept
	{
		shared_lock l{ _accountMutex };
		 if( !allowDeleted && _deletedAccounts.find( string{name} )!=_deletedAccounts.end() )
		 	return false;
		bool haveAccess = false;

		if( var pIdAccount = _accounts.find( string{name} ); pIdAccount!=_accounts.end() )
		{
			var& account = pIdAccount->second;
			if( var pAccount = account.Access.find(userId); pAccount != account.Access.end() )
				haveAccess = (pAccount->second & requested)!=UM::EAccess::None;
			else if( var pAccount2 = account.Access.find(std::numeric_limits<UserPK>::max()); userId!=0 && pAccount2 != account.Access.end() )
				haveAccess = (pAccount2->second & requested)!=UM::EAccess::None;
		}
		if( var pUser = _minimumAccess.find(userId); !haveAccess && pUser!=_minimumAccess.end() )
			haveAccess = (pUser->second & requested)!=UM::EAccess::None;

		return haveAccess;
	}

	void WrapperWeb::nextValidId( ::OrderId orderId)noexcept
	{
		WrapperLog::nextValidId( orderId );
		TwsClientSync::Instance().SetRequestId( orderId );
	}

	void WrapperWeb::orderStatus( ::OrderId orderId, const std::string& status, double filled, double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )noexcept
	{
		WrapperLog::orderStatus( orderId, status, filled,	remaining, avgFillPrice, permId, parentId, lastFillPrice, clientId, whyHeld, mktCapPrice );
		if( !_pWebSend ) return;
		auto p = make_unique<Proto::Results::OrderStatus>();
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
		//could subscribe to all orders or just the one.
		_pWebSend->Push( EResults::OrderStatus_, [&p](MessageType& msg){msg.set_allocated_order_status(new Proto::Results::OrderStatus{*p});} );
		_pWebSend->Push( orderId, [&p](MessageType& msg, ClientPK id){p->set_id( id ); msg.set_allocated_order_status( p.release() );} );
	}
	void WrapperWeb::openOrder( ::OrderId orderId, const ::Contract& contract, const ::Order& order, const ::OrderState& state )noexcept
	{
		WrapperLog::openOrder( orderId, contract, order, state );
		if( !_pWebSend ) return;
		auto p = make_unique<Proto::Results::OpenOrder>();
		p->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		p->set_allocated_order( MyOrder{order}.ToProto().release() );
		p->set_allocated_state( MyOrder::ToAllocatedProto(state) );
		_pWebSend->Push( EResults::OpenOrder_, [&p](MessageType& msg){msg.set_allocated_open_order(new Proto::Results::OpenOrder{*p});} );
		_pWebSend->Push( orderId, [&p](MessageType& msg, ClientPK id){p->set_web_id(id); msg.set_allocated_open_order( p.release() );} );
	}
	void WrapperWeb::openOrderEnd()noexcept
	{
		WrapperLog::openOrderEnd();
		_webSend.Push( EResults::OpenOrderEnd, [](MessageType& msg){msg.set_type(EResults::OpenOrderEnd);} );
	}
	void WrapperWeb::positionMulti( int reqId, const std::string& account, const std::string& modelCode, const ::Contract& contract, double pos, double avgCost )noexcept
	{
		WrapperLog::positionMulti( reqId, account, modelCode, contract, pos, avgCost );
		auto pUpdate = make_unique<Proto::Results::PositionMulti>();
		pUpdate->set_account( account );
		pUpdate->set_allocated_contract( Contract{contract}.ToProto(true).get() );
		pUpdate->set_position( pos );
		pUpdate->set_avgerage_cost( avgCost );
		pUpdate->set_model_code( modelCode );

		_pWebSend->Push( reqId, [&p=pUpdate](MessageType& msg, ClientPK id){p->set_id( id ); msg.set_allocated_position_multi(p.release());} );
	}
	void WrapperWeb::positionMultiEnd( int reqId )noexcept
	{
		WrapperLog::positionMultiEnd( reqId );
		_pWebSend->Push( EResults::PositionMultiEnd, reqId );
	}
	void WrapperWeb::LoadAccess()noexcept
	{
		auto pDataSource = DB::DataSource(); RETURN_IF( !pDataSource, "No Datasource"sv );
		unique_lock l{ _accountMutex };
		_deletedAccounts.clear();
		_accounts.clear();
		var deleted = DB::DefaultSyntax()->DateTimeSelect("deleted");
		if( !pDataSource->TrySelect( format("select name, description, {}, id from ib_accounts", deleted), [&]( const DB::IRow& row )
		{
			var name = row.GetString( 0 ); var description = row.GetString( 1 ); var pDeleted = row.GetDateTimeOpt( 2 );
			if( pDeleted )
				_deletedAccounts.emplace( name );
			else
				_accounts.emplace( name, Account{description.size() ? description : name, row.GetUInt(3)} );
		}) ) return;
		pDataSource->TrySelect( "SELECT account_id, user_id, right_id  FROM ib_account_roles ib join um_group_roles gr on gr.role_id=ib.role_id join um_user_groups ug on gr.group_id=ug.user_id", [&]( const DB::IRow& row )
		{
			var accountId = row.GetUInt( 0 );
			var pAccount = std::find_if( _accounts.begin(), _accounts.end(), [&]( var& x ){ return x.second.Id==accountId; } ); THROW_IF( pAccount==_accounts.end(), Exception(std::to_string(accountId)) );
			pAccount->second.Access.emplace( (UserPK)row.GetUInt(1), (UM::EAccess)row.GetUInt16(2) );
		} );
	}
	void WrapperWeb::LoadMinimumAccess()noexcept
	{
		auto pDataSource = DB::DataSource(); RETURN_IF( !pDataSource, "No Datasource"sv );
		unique_lock l{ _accountMutex };
		_minimumAccess.clear();
		if( !pDataSource->TrySelect( "SELECT user_id, right_id from um_permissions p join um_apis apis on p.api_id=apis.id and apis.name='TWS' and p.name is null join um_role_permissions rp on rp.permission_id=p.id join um_group_roles gr on gr.role_id=rp.role_id join um_user_groups ug on gr.group_id=ug.user_id", [&]( const DB::IRow& row )
		{
			_minimumAccess[(UserPK)row.GetUInt(0)] = (UM::EAccess)row.GetUInt16( 1 );
		}) ) return;

	}
	static bool _setAccounts{false};
	void WrapperWeb::managedAccounts( const std::string& accountsList )noexcept
	{
		WrapperLog::managedAccounts( accountsList );
		Proto::Results::StringMap accountList;
		var accounts = Str::Split( accountsList );
		if( !_setAccounts )
		{
			LoadAccess();
			LoadMinimumAccess();
			shared_lock l{ _accountMutex };
			auto pDataSource = DB::DataSource(); RETURN_IF( !pDataSource, "No Datasource"sv );
			for( var& name : accounts )
			{
				if( !_deletedAccounts.contains(name) && !_accounts.contains(name) && pDataSource->TryExecuteProc("ib_account_insert( ?, 0, ?, ? )", {name,nullptr,name}) )
				{
					_accounts.emplace( name, Account{name} );
					INFO( "inserted account '{}'."sv, name );
				}
			}
			DB::AddMutationListener( "ib", [this](const DB::MutationQL& m, PK /*id*/)
			{
				if( m.JsonName.ends_with( "AccountRoles") || m.JsonName.ends_with( "RoleAccounts") )
					LoadAccess();
			} );
			DB::AddMutationListener( "um", [this]( const DB::MutationQL& m, PK /*id*/ )
			{
				if( m.JsonName.ends_with( "RolePermissions") || m.JsonName.ends_with( "PermissionRoles") )
					LoadMinimumAccess();
			} );
		}
		_setAccounts = true;
		shared_lock l{ _accountMutex };
		for( var& account : accounts )
		{
			//if( !_deletedAccounts.contains(account) )
			(*accountList.mutable_values())[account] = _accounts.find(account)!=_accounts.end() ? _accounts.find(account)->second.Description : account;
		}
		_webSend.Push( EResults::ManagedAccounts, [&accountList, this]( auto& m, SessionPK sessionId )
		{
			var pSocket = WebCoSocket::Instance(); if( !pSocket ) return;
			var userId = pSocket->TryUserId( sessionId );
			Proto::Results::StringMap userList; userList.set_result( EResults::ManagedAccounts );
			for( var& [name,description] : accountList.values() )
			{
				if( tryTestAccess(UM::EAccess::Read, userId, name) )
					(*userList.mutable_values())[name] = description;
			}
			m.set_allocated_string_map( new Proto::Results::StringMap{userList} );
		});
	}
/*	void WrapperWeb::accountDownloadEnd( const std::string& accountName )noexcept
	{
		WrapperLog::accountDownloadEnd( accountName );
		_pWebSend->AccountDownloadEnd( accountName );
	}*/
	void WrapperWeb::accountUpdateMulti( int reqId, const std::string& accountName, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept
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
	void WrapperWeb::accountUpdateMultiEnd( int reqId )noexcept
	{
		WrapperLog::accountUpdateMultiEnd( reqId );
		auto pValue = new Proto::Results::MessageValue(); pValue->set_type( Proto::Results::EResults::PositionMultiEnd );// pValue->set_int_value( reqId );
		//_pWebSend->PushAllocated( pValue, reqId );
		_pWebSend->Push( reqId, [pValue](MessageType& msg, ClientPK id){ pValue->set_int_value(id); msg.set_allocated_message(pValue); } );
	}
	void WrapperWeb::historicalData( TickerId reqId, const ::Bar& bar )noexcept
	{
		if( Cache::TryGet<uint>("breakpoint.BGGSQ") && *Cache::TryGet<uint>("breakpoint.BGGSQ")==reqId )
			TRACE( "Break here."sv );
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
		TRY( _pWebSend->Push(reqId, [p=pData.release()](MessageType& msg, ClientPK id){ p->set_request_id(id); msg.set_allocated_historical_data(p); }) );
		_historicalData.erase( reqId );
	}

	void WrapperWeb::error( int reqId, int errorCode, const std::string& errorString )noexcept
	{
		if( !WrapperSync::error2(reqId, errorCode, errorString) )
		{
			if( errorCode==162 && _pWebSend->HasHistoricalRequest(reqId) )// _historicalCrcs.Has(id)
				_pWebSend->Push( reqId, [](MessageType& msg, ClientPK id){ auto p=new Proto::Results::HistoricalData{}; p->set_request_id(id); msg.set_allocated_historical_data(p); } );
			else
				_pWebSend->PushError( errorCode, errorString, reqId );
		}
	}

	void WrapperWeb::HandleBadTicker( TickerId reqId )noexcept
	{
		if( _canceledItems.emplace(reqId) )
		{
			DBG( "Could not find session for ticker req:  '{}'."sv, reqId );
			TwsClientSync::Instance().cancelMktData( reqId );
		}
	}
/*	void WrapperWeb::tickPrice( TickerId reqId, TickType field, double price, const TickAttrib& attrib )noexcept
	{
		if( WrapperSync::TickPrice(reqId, field, price, attrib) )
			return;

		_pTickWorker->PushPrice( reqId, (ETickType)field, price );
		// auto pAttributes = new Proto::Results::TickAttrib(); pAttributes->set_can_auto_execute( attrib.canAutoExecute ); pAttributes->set_past_limit( attrib.pastLimit ); pAttributes->set_pre_open( attrib.preOpen );
		// auto p = new Proto::Results::TickPrice(); p->set_tick_type( (ETickType)field ); p->set_price( price ); p->set_allocated_attributes( pAttributes );
		// _pWebSend->PushMarketData( reqId, [p](MessageType& msg, ClientPK id){ p->set_request_id(id); msg.set_allocated_tick_price(p); } );
	}
	void WrapperWeb::tickSize( TickerId reqId, TickType field, int size )noexcept
	{
		if( WrapperSync::TickSize(reqId, field, size) )
			return;
		_pTickWorker->Push( reqId, (ETickType)field, size );
		// auto p = new Proto::Results::TickSize(); p->set_tick_type( (ETickType)field ); p->set_size( size );
		// _pWebSend->PushMarketData( reqId, [p]( MessageType& msg, ClientPK id ){ p->set_request_id(id); msg.set_allocated_tick_size(p); } );
	}

	void WrapperWeb::tickGeneric( TickerId reqId, TickType field, double value )noexcept
	{
		if( WrapperSync::TickGeneric(reqId, field, value) )
			return;

		_pTickWorker->Push( reqId, (ETickType)field, value );
		//auto p = new Proto::Results::TickGeneric(); p->set_tick_type( (ETickType)field ); p->set_value( value );
		//_pWebSend->PushMarketData( reqId, [p](MessageType& msg, ClientPK id){ p->set_request_id(id); msg.set_allocated_tick_generic(p); } );
	}

	void WrapperWeb::tickOptionComputation( TickerId reqId, TickType tickType, int tickAttrib, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice )noexcept
	{
		_pTickWorker->Push( reqId, (ETickType)tickType, tickAttrib, impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, undPrice );
		//auto p = make_unique<Proto::Results::OptionCalculation>(); p->set_tick_type( (ETickType)tickType ); p->set_price_based( tickAttrib==1 ); p->set_implied_volatility( impliedVol ); p->set_delta( delta ); p->set_option_price( optPrice ); p->set_pv_dividend( pvDividend ); p->set_gamma( gamma ); p->set_vega( vega ); p->set_theta( theta ); p->set_underlying_price( undPrice );
		//_pWebSend->PushMarketData( reqId, [&p2=p](MessageType& msg, ClientPK id){ p2->set_request_id(id); msg.set_allocated_option_calculation(p2.release());} );
	}
	void WrapperWeb::tickNews( int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData )noexcept
	{
		_pTickWorker->Push( tickerId, timeStamp, providerCode, articleId, headline, extraData );
		// auto pUpdate = new Proto::Results::TickNews();//TODO remove all new
		// pUpdate->set_time( static_cast<uint32>(timeStamp) );
		// pUpdate->set_provider_code( providerCode );
		// pUpdate->set_article_id( articleId );
		// pUpdate->set_headline( headline );
		// pUpdate->set_extra_data( extraData );

		// _pWebSend->PushMarketData( tickerId, [p=pUpdate](MessageType& msg, ClientPK id){p->set_id( id ); msg.set_allocated_tick_news(p);} );
	}

	void WrapperWeb::tickString( TickerId reqId, TickType field, const std::string& value )noexcept
	{
		if( WrapperSync::TickString(reqId, field, value) )
			return;
		_pTickWorker->Push( reqId, (ETickType)field, value );
//		auto p = new Proto::Results::TickString();  p->set_tick_type( (ETickType)field ); p->set_value( value );
//		_pWebSend->PushMarketData( reqId, [p](MessageType& msg, ClientPK id){ p->set_request_id(id); msg.set_allocated_tick_string(p); } );
	}
*/
	void WrapperWeb::tickSnapshotEnd( int reqId )noexcept
	{
		WrapperLog::tickSnapshotEnd( reqId );
		_pWebSend->Push( Proto::Results::EResults::TickSnapshotEnd, reqId );
	}

/*	void WrapperWeb::CancelAccountUpdate( sv accountNumber, unique_lock<mutex>* pLock )noexcept
	{
		//unique_lock l2{ _canceledAccountMutex };
		bool found = false;
		for( auto p = _canceledAccounts.begin(); p!=_canceledAccounts.end(); )//see if already canceled, and remove old canceled records.
		{
			if( p->first==accountNumber )
				found = true;
			p = !found && p->second< Clock::now()-1min ? _canceledAccounts.erase( p ) : next( p );
		}
		if( !found )
		{
			_canceledAccounts.emplace( accountNumber, Clock::now() ); TRACE( "No current listeners for account update '{}', req AccountUpdates"sv, accountNumber );
			_client.reqAccountUpdates( false, string{accountNumber} );
			_accountUpdates.clear();
			_accountPortfolioUpdates.clear();
		}
	}
*/
/*	void WrapperWeb::updateAccountValue( const std::string& key, const std::string& value, const std::string& currency, const std::string& accountName )noexcept
	{
		var haveCallback = WrapperLog::updateAccountValue2( key, value, currency, accountName );
	/ *	Proto::Results::AccountUpdate update;
		update.set_account( accountName );
		update.set_key( key );
		update.set_value( value );
		update.set_currency( currency );
* /
		unique_lock l{ _accountUpdateMutex };
		if( / *!_pWebSend->Push(update) &&* / !haveCallback )
			CancelAccountUpdate( accountName, &l );
		else
			_accountUpdates[accountName][key]=update;
	}

	void WrapperWeb::updatePortfolio( const ::Contract& contract, double position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountNumber )noexcept
	{
		WrapperLog::updatePortfolio( contract, position, marketPrice, marketValue, averageCost, unrealizedPNL, realizedPNL, accountNumber );
		Proto::Results::PortfolioUpdate update;
		Contract myContract{ contract };
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

		unique_lock l{ _accountUpdateMutex };
		if( !_pWebSend->Push(update) )
			CancelAccountUpdate( accountNumber, &l );
		else
			_accountPortfolioUpdates[accountNumber][contract.conId]=update;
	}
*/
	void WrapperWeb::updateAccountTime( const std::string& timeStamp )noexcept
	{
		WrapperLog::updateAccountTime( timeStamp );//not sure what to do about this, no reqId or accountName
	}

	void WrapperWeb::contractDetails( int reqId, const ::ContractDetails& contractDetails )noexcept
	{
		if( WrapperCo::_contractSingleHandles.Has(reqId) )
			WrapperCo::contractDetails( reqId, contractDetails );
		else if( _detailsData.Contains(reqId) )
			WrapperSync::contractDetails( reqId, contractDetails );
		else
		{
			WrapperLog::contractDetails( reqId, contractDetails );
			auto pReqDetails = _contractDetails.find( reqId );
			if( ; pReqDetails==_contractDetails.end() )
				pReqDetails = _contractDetails.emplace( reqId, make_unique<Proto::Results::ContractDetailsResult>() ).first;
			ToProto( contractDetails, *pReqDetails->second->add_details() );
		}
	}
	void WrapperWeb::contractDetailsEnd( int reqId )noexcept
	{
		if( WrapperCo::_contractSingleHandles.Has(reqId) )
			WrapperCo::contractDetailsEnd( reqId );
		else if( _detailsData.Contains(reqId) )
			WrapperSync::contractDetailsEnd( reqId );
		else
		{
			WrapperLog::contractDetailsEnd( reqId );
			var p = _contractDetails.find( reqId );
			var have = p !=_contractDetails.end();
			_pWebSend->ContractDetails( have ? std::move(p->second) : make_unique<Proto::Results::ContractDetailsResult>(), reqId );
		}
	}

	void WrapperWeb::commissionReport( const CommissionReport& ib )noexcept
	{
		Proto::Results::CommissionReport a; a.set_exec_id( ib.execId ); a.set_commission( ib.commission ); a.set_currency( ib.currency ); a.set_realized_pnl( ib.realizedPNL ); a.set_yield( ib.yield ); a.set_yield_redemption_date( ib.yieldRedemptionDate );
		_pWebSend->Push( a );
	}
	void WrapperWeb::execDetails( int reqId, const ::Contract& contract, const Execution& ib )noexcept
	{
		auto p = make_unique<Proto::Results::Execution>();
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
		_pWebSend->Push( reqId, [&p](MessageType& msg, ClientPK id){ p->set_id( id ); msg.set_allocated_execution( p.release() );} );

	}
	void WrapperWeb::execDetailsEnd( int reqId )noexcept
	{
		WrapperLog::execDetailsEnd( reqId );
		_pWebSend->Push( EResults::ExecutionDataEnd, reqId );
	}

	// void WrapperWeb::newsArticle( int requestId, int articleType, const std::string& articleText )noexcept
	// {
	// 	auto p = make_unique<Proto::Results::NewsArticle>();
	// 	p->set_is_text( articleType==0 );
	// 	p->set_value( articleText );
	// 	_pWebSend->TryPush( requestId, [&p2=p](MessageType& msg, ClientPK id){p2->set_request_id( id ); msg.set_allocated_news_article(p2.release());} );
	// }
/*	void WrapperWeb::historicalNews( int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline )noexcept
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
		_pWebSend->Push( requestId, [p=pCollection](MessageType& msg, ClientPK id){p->set_request_id( id ); msg.set_allocated_historical_news(p);} );
	}
*/
/*	void WrapperWeb::newsProviders( const std::vector<NewsProvider>& providers, bool isCache )noexcept
	{
		if( !isCache )
			WrapperCache::newsProviders( providers );
		Proto::Results::StringMap map;
		map.set_result( EResults::NewsProviders );
		for( var& provider : providers )
			(*map.mutable_values())[provider.providerCode] = provider.providerName;

		_pWebSend->Push( EResults::NewsProviders, [&map]( auto& type ){ type.set_allocated_string_map(new Proto::Results::StringMap{map});} );
	}
*/
	void WrapperWeb::securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept
	{
		var handled = WrapperSync::securityDefinitionOptionalParameterSync( reqId, exchange, underlyingConId, tradingClass, multiplier, expirations, strikes );
		if( !handled && CIString{exchange}=="SMART"sv )
			*Collections::InsertUnique( _optionParams, reqId )->add_exchanges() = ToOptionParam( exchange, underlyingConId, tradingClass, multiplier, expirations, strikes );
	}

	void WrapperWeb::securityDefinitionOptionalParameterEnd( int reqId )noexcept
	{
		bool handled = WrapperSync::securityDefinitionOptionalParameterEndSync( reqId );
		if( !handled )
		{
			unique_ptr<Proto::Results::OptionExchanges> pExchanges;
			if( auto pReqExchanges = _optionParams.find( reqId ); pReqExchanges!=_optionParams.end() )
			{
				pExchanges = move( pReqExchanges->second );
				_optionParams.erase( pReqExchanges );
			}
			else
				pExchanges = make_unique<Proto::Results::OptionExchanges>();

			auto p = pExchanges.release();
			if( !_pWebSend->Push(reqId, [p](MessageType& msg, ClientPK id)mutable{p->set_request_id( id ); msg.set_allocated_option_exchanges(p);}) )
				delete p;
		}
	}
}