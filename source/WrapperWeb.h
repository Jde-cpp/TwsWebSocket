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

		void accountUpdateMulti( int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept override;
		void accountUpdateMultiEnd( int reqId )noexcept override;

		void contractDetails( int reqId, const ContractDetails& contractDetails)noexcept override;
		void contractDetailsEnd( int reqId)noexcept override;
		void commissionReport( const CommissionReport& commissionReport)noexcept override;
		void execDetails( int reqId, const ::Contract& contract, const Execution& execution)noexcept override;
		void execDetailsEnd( int reqId)noexcept override;

		void historicalData( TickerId reqId, const ::Bar& bar )noexcept override;
		void historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )noexcept override;
		void error(int id, int errorCode, const std::string& errorString)noexcept override;
		void managedAccounts( const std::string& accountsList)noexcept override;
		void nextValidId( ::OrderId orderId )noexcept override;
		void orderStatus( ::OrderId orderId, const std::string& status, double filled, double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )noexcept override;
		void openOrder( ::OrderId orderId, const ::Contract&, const ::Order&, const ::OrderState&)noexcept override;
		void openOrderEnd()noexcept override;
		void positionMulti( int reqId, const std::string& account,const std::string& modelCode, const ::Contract& contract, double pos, double avgCost)noexcept override;
		void positionMultiEnd( int reqId)noexcept override;

		void tickSnapshotEnd( int reqId)noexcept override;
		void updateAccountTime( const std::string& timeStamp )noexcept override;

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
		void securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept override;
		void securityDefinitionOptionalParameterEnd( int reqId )noexcept override;
		void HandleBadTicker( TickerId ibReqId )noexcept;
		static sp<WrapperWeb> _pInstance;

		flat_map<TickerId,up<Proto::Results::HistoricalData>> _historicalData; mutex _historicalDataMutex;
		flat_map<int,up<Proto::Results::OptionExchanges>> _optionParams;
		struct Account{ string Description; PK Id{0}; flat_map<UserPK,UM::EAccess> Access; };
		flat_map<string,Account> _accounts; shared_mutex _accountMutex;  flat_set<string> _deletedAccounts; flat_map<UserPK,UM::EAccess> _minimumAccess;
		UnorderedSet<TickerId> _canceledItems;

		flat_map<int,unique_ptr<Proto::Results::ContractDetailsResult>> _contractDetails;
		sp<WebSendGateway> _pWebSend;
	};
}