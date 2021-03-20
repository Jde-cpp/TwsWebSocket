#pragma once
#include "../../MarketLibrary/source/wrapper/WrapperSync.h"
#include "../../MarketLibrary/source/types/TwsConnectionSettings.h"
struct EReaderSignal;
namespace Jde::UM{ enum class EAccess : uint8; }
namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway;
	enum class EWebReceive : short;

	struct WrapperWeb : public WrapperSync
	{
		static tuple<sp<TwsClientSync>,sp<WrapperWeb>> CreateInstance()noexcept(false);
		static WrapperWeb& Instance()noexcept;
		bool HaveInstance()const noexcept{ return _pInstance!=nullptr; }

	//	static bool HaveAccountUpdateCallbacks()noexcept{ auto p = _pInstance; if(!p) return false; shared_lock l{p->_accountUpdateCallbackMutex}; return p->_accountUpdateCallbacks.size(); };
		//void CancelAccountUpdate( sv accountName )noexcept;

		//void accountDownloadEnd( const std::string& accountName )noexcept override;
		void accountUpdateMulti( int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept override;
		void accountUpdateMultiEnd( int reqId )noexcept override;
		//void accountSummary( int reqId, const std::string& account, const std::string& tag, const std::string& value, const std::string& curency)noexcept override;
		//void accountSummaryEnd( int reqId)noexcept override;

		void contractDetails( int reqId, const ContractDetails& contractDetails)noexcept override;
		void contractDetailsEnd( int reqId)noexcept override;
		void commissionReport( const CommissionReport& commissionReport)noexcept override;
		void execDetails( int reqId, const ::Contract& contract, const Execution& execution)noexcept override;
		void execDetailsEnd( int reqId)noexcept override;

		void historicalData( TickerId reqId, const ::Bar& bar )noexcept override;
		void historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )noexcept override;
		void error(int id, int errorCode, const std::string& errorString)noexcept override;
		void managedAccounts( const std::string& accountsList)noexcept override;
		void newsProviders( const std::vector<NewsProvider>& providers )noexcept override{ newsProviders( providers, false ); }
		void newsProviders( const std::vector<NewsProvider>& providers, bool isCache )noexcept;
		void nextValidId( ::OrderId orderId )noexcept override;
		void orderStatus( ::OrderId orderId, const std::string& status, double filled, double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice )noexcept override;
		void openOrder( ::OrderId orderId, const ::Contract&, const ::Order&, const ::OrderState&)noexcept override;
		void openOrderEnd()noexcept override;
		void positionMulti( int reqId, const std::string& account,const std::string& modelCode, const ::Contract& contract, double pos, double avgCost)noexcept override;
		void positionMultiEnd( int reqId)noexcept override;

		//void tickGeneric(TickerId tickerId, TickType tickType, double value)noexcept override;
		//void tickOptionComputation( TickerId tickerId, TickType tickType, int tickAttrib, double impliedVol, double delta,	double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice)noexcept override;
		//void tickPrice( TickerId tickerId, TickType field, double price, const TickAttrib& attrib)noexcept override;
		//void tickSize( TickerId tickerId, TickType field, int size)noexcept override;
		void tickSnapshotEnd( int reqId)noexcept override;
		//void tickString(TickerId tickerId, TickType tickType, const std::string& value)noexcept override;

		//need to cache values between calls.
		//void updateAccountValue( const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName )noexcept override;
		//void updatePortfolio( const ::Contract& contract, double position, double marketPrice, double marketValue, double /*averageCost*/, double /*unrealizedPNL*/, double /*realizedPNL*/, const std::string& /*accountName*/)noexcept  override;
		void updateAccountTime( const std::string& timeStamp )noexcept override;

		bool AddCanceled( TickerId id )noexcept{ return _canceledItems.emplace(id); }

		//void tickNews( int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData )noexcept override;
		void newsArticle( int requestId, int articleType, const std::string& articleText )noexcept override;
		void historicalNews( int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline )noexcept override;
		void historicalNewsEnd( int requestId, bool hasMore )noexcept override;
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
		//sp<vector<Proto::Results::OptionParams>> securityDefinitionOptionalParameterEndSync( int reqId )noexcept override;
		void securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept override;
		void securityDefinitionOptionalParameterEnd( int reqId )noexcept override;
		void HandleBadTicker( TickerId ibReqId )noexcept;
		static sp<WrapperWeb> _pInstance;

		//vector<function<void(const Proto::Results::AccountUpdate&)> _accountCallbacks;
		flat_map<TickerId,up<Proto::Results::HistoricalData>> _historicalData; mutex _historicalDataMutex;
		flat_map<int,up<Proto::Results::OptionExchanges>> _optionParams;
		struct Account{ string Description; PK Id{0}; flat_map<UserPK,UM::EAccess> Access; };
		flat_map<string,Account> _accounts; shared_mutex _accountMutex;  flat_set<string> _deletedAccounts; flat_map<UserPK,UM::EAccess> _minimumAccess;
		UnorderedSet<TickerId> _canceledItems;

		//void CancelAccountUpdate( sv accountName, unique_lock<mutex>* pLock )noexcept;
	//	flat_map<string,TimePoint> _canceledAccounts; mutable std::mutex _canceledAccountMutex;//ie ask for multiple contractDetails

		flat_map<int,unique_ptr<Proto::Results::ContractDetailsResult>> _contractDetails;
		flat_map<TickerId,Proto::Results::HistoricalNewsCollection*> _allocatedNews; mutex _newsMutex;
		//mutable std::mutex _accountUpdateMutex;
		sp<WebSendGateway> _pWebSend;
	};
}