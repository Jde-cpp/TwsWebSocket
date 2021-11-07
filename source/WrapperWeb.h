#pragma once
#include "../../MarketLibrary/source/wrapper/WrapperSync.h"
#include "../../MarketLibrary/source/types/TwsConnectionSettings.h"
struct EReaderSignal;
namespace Jde::UM{ enum class EAccess : uint8; }
namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway;
	enum class EWebReceive : short;

	struct WrapperWeb : WrapperSync
	{
		static tuple<sp<TwsClientSync>,sp<WrapperWeb>> CreateInstance()noexcept(false);
		static WrapperWeb& Instance()noexcept;
		bool HaveInstance()const noexcept{ return _pInstance!=nullptr; }

		void accountUpdateMulti( int reqId, str account, str modelCode, str key, str value, str currency )noexcept override;
		void accountUpdateMultiEnd( int reqId )noexcept override;

		void contractDetails( int reqId, const ContractDetails& contractDetails)noexcept override;
		void contractDetailsEnd( int reqId)noexcept override;
		void commissionReport( const CommissionReport& commissionReport)noexcept override;
		void execDetails( int reqId, const ::Contract& contract, const Execution& execution)noexcept override;
		void execDetailsEnd( int reqId)noexcept override;

		//void historicalData( TickerId reqId, const ::Bar& bar )noexcept override;
		//void historicalDataEnd( int reqId, str startDateStr, str endDateStr )noexcept override;
		void error(int id, int errorCode, str errorString)noexcept override;
		void managedAccounts( str accountsList)noexcept override;
		void nextValidId( ::OrderId orderId )noexcept override;
		void orderStatus( ::OrderId orderId, str status, ::Decimal filled, ::Decimal remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, str whyHeld, double mktCapPrice )noexcept override;
		void openOrder( ::OrderId orderId, const ::Contract&, const ::Order&, const ::OrderState&)noexcept override;
		void openOrderEnd()noexcept override;
		void positionMulti( int reqId, str account,str modelCode, const ::Contract& contract, ::Decimal pos, double avgCost)noexcept override;
		void positionMultiEnd( int reqId)noexcept override;

		void tickSnapshotEnd( int reqId)noexcept override;
		void updateAccountTime( str timeStamp )noexcept override;

		bool AddCanceled( TickerId id )noexcept{ return _canceledItems.emplace(id); }

		void SetWebSend( sp<WebSendGateway> pWebSend )noexcept{ _pWebSend = pWebSend; }
		void AddAccountUpdateCallback( string account, function<void(const Proto::Results::AccountUpdate&)> callback )noexcept;

		static bool TryTestAccess( UM::EAccess access, sv name, SessionPK sessionId, bool allowDeleted=false )noexcept;
		static bool TryTestAccess( UM::EAccess access, UserPK userId, sv name, bool allowDeleted=false )noexcept;
		bool tryTestAccess( UM::EAccess access, sv name, SessionPK sessionId, bool allowDeleted=false )noexcept;
		bool tryTestAccess( UM::EAccess access, UserPK userId, sv name, bool allowDeleted=false )noexcept;
	private:
		void LoadAccess()noexcept;
		void LoadMinimumAccess()noexcept;
		WrapperWeb()noexcept;
		sp<TwsClientSync> CreateClient( uint twsClientId )noexcept(false) override;
		//void securityDefinitionOptionalParameter( int reqId, str exchange, int underlyingConId, str tradingClass, str multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept override;
		//void securityDefinitionOptionalParameterEnd( int reqId )noexcept override;
		void HandleBadTicker( TickerId ibReqId )noexcept;
		static sp<WrapperWeb> _pInstance;

		//flat_map<TickerId,up<Proto::Results::HistoricalData>> _historicalData; mutex _historicalDataMutex;
		flat_map<int,up<Proto::Results::OptionExchanges>> _optionParams;
		struct Account{ string Description; PK Id{0}; flat_map<UserPK,UM::EAccess> Access; };
		flat_map<string,Account> _accounts; shared_mutex _accountMutex;  flat_set<string> _deletedAccounts; flat_map<UserPK,UM::EAccess> _minimumAccess;
		UnorderedSet<TickerId> _canceledItems;

		flat_map<int,unique_ptr<Proto::Results::ContractDetailsResult>> _contractDetails;
		sp<WebSendGateway> _pWebSend;
	};
}