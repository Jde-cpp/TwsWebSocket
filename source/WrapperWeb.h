#pragma once
#include "../../MarketLibrary/source/wrapper/WrapperSync.h"

namespace Jde::UM{ enum class EAccess : uint8; }

namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway;
	enum class EWebReceive : short;

	struct WrapperWeb : WrapperSync
	{
		Ω CreateInstance()noexcept(false)->tuple<sp<TwsClientSync>,sp<WrapperWeb>>;
		Ω Instance()noexcept->WrapperWeb&;
		α HaveInstance()const noexcept{ return _pInstance!=nullptr; }

		α accountUpdateMulti( int reqId, str account, str modelCode, str key, str value, str currency )noexcept->void override;
		α accountUpdateMultiEnd( int reqId )noexcept->void override;

		α contractDetails( int reqId, const ContractDetails& contractDetails)noexcept->void override;
		α contractDetailsEnd( int reqId)noexcept->void override;
		α commissionReport( const CommissionReport& commissionReport)noexcept->void override;
		α execDetails( int reqId, const ::Contract& contract, const Execution& execution)noexcept->void override;
		α execDetailsEnd( int reqId)noexcept->void override;

		α error(int id, int errorCode, str errorString)noexcept->void override;
		α managedAccounts( str accountsList)noexcept->void override;
		α nextValidId( ::OrderId orderId )noexcept->void override;
		α orderStatus( ::OrderId orderId, str status, ::Decimal filled, ::Decimal remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, str whyHeld, double mktCapPrice )noexcept->void override;
		α openOrder( ::OrderId orderId, const ::Contract&, const ::Order&, const ::OrderState&)noexcept->void override;
		α openOrderEnd()noexcept->void override;
		α positionMulti( int reqId, str account,str modelCode, const ::Contract& contract, ::Decimal pos, double avgCost)noexcept->void override;
		α positionMultiEnd( int reqId)noexcept->void override;

		α tickSnapshotEnd( int reqId)noexcept->void override;
		α updateAccountTime( str timeStamp )noexcept->void override;

		α AddCanceled( TickerId id )noexcept{ return _canceledItems.emplace(id); }

		α SetWebSend( sp<WebSendGateway> pWebSend )noexcept{ _pWebSend = pWebSend; }
		α AddAccountUpdateCallback( string account, function<void(const Proto::Results::AccountUpdate&)> callback )noexcept->void;

		Ω TryTestAccess( UM::EAccess access, sv name, SessionPK sessionId, bool allowDeleted=false )noexcept->bool;
		Ω TryTestAccess( UM::EAccess access, UserPK userId, sv name, bool allowDeleted=false )noexcept->bool;
		α tryTestAccess( UM::EAccess access, sv name, SessionPK sessionId, bool allowDeleted=false )noexcept->bool;
		α tryTestAccess( UM::EAccess access, UserPK userId, sv name, bool allowDeleted=false )noexcept->bool;
	private:
		WrapperWeb()noexcept;

		α LoadAccess()noexcept->void;
		α LoadMinimumAccess()noexcept->void;
		α CreateClient( uint twsClientId )noexcept(false)->sp<TwsClientSync> override;
		α HandleBadTicker( TickerId ibReqId )noexcept->void;

		static sp<WrapperWeb> _pInstance;
		flat_map<int,up<Proto::Results::OptionExchanges>> _optionParams;
		struct Account{ string Description; PK Id{0}; flat_map<UserPK,UM::EAccess> Access; };
		flat_map<string,Account> _accounts; shared_mutex _accountMutex;  flat_set<string> _deletedAccounts; flat_map<UserPK,UM::EAccess> _minimumAccess;
		UnorderedSet<TickerId> _canceledItems;

		flat_map<int,up<Proto::Results::ContractDetailsResult>> _contractDetails;
		sp<WebSendGateway> _pWebSend;
	};
}