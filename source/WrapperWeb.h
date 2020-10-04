#pragma once
#include "../../MarketLibrary/source/wrapper/WrapperSync.h"
#include "../../MarketLibrary/source/types/TwsConnectionSettings.h"
struct EReaderSignal;
namespace Jde::Markets::TwsWebSocket
{
	enum class EWebReceive : short;

	struct WrapperWeb : public WrapperSync
	{
		static void CreateInstance( const TwsConnectionSettings& settings )noexcept;
		static WrapperWeb& Instance()noexcept;
		bool HaveInstance()const noexcept{ return _pInstance!=nullptr; }
		void accountDownloadEnd( const std::string& accountName )noexcept override;
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
		void newsProviders( const std::vector<NewsProvider>& providers )noexcept override{ newsProviders( providers, false ); }
		void newsProviders( const std::vector<NewsProvider>& providers, bool isCache )noexcept;
		void nextValidId( ::OrderId orderId )noexcept override;
		void orderStatus( ::OrderId orderId, const std::string& status, double filled,	double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice)noexcept override;
		void openOrder( ::OrderId orderId, const ::Contract&, const ::Order&, const ::OrderState&)noexcept override;
		void openOrderEnd()noexcept override;
		void positionMulti( int reqId, const std::string& account,const std::string& modelCode, const ::Contract& contract, double pos, double avgCost)noexcept override;
		void positionMultiEnd( int reqId)noexcept override;

		void tickPrice( TickerId tickerId, TickType field, double price, const TickAttrib& attrib)noexcept override;
		void tickSize( TickerId tickerId, TickType field, int size)noexcept override;
		void tickGeneric(TickerId tickerId, TickType tickType, double value)noexcept override;
		void tickString(TickerId tickerId, TickType tickType, const std::string& value)noexcept override;
		void tickSnapshotEnd( int reqId)noexcept override;

		//need to cache values between calls.
		void updateAccountValue( const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName )noexcept override;
		void updatePortfolio( const ::Contract& contract, double position, double marketPrice, double marketValue, double /*averageCost*/, double /*unrealizedPNL*/, double /*realizedPNL*/, const std::string& /*accountName*/)noexcept  override;
		void updateAccountTime( const std::string& timeStamp )noexcept override;

		bool AddCanceled( TickerId id )noexcept{ return _canceledItems.emplace(id); }

		void tickNews( int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData )noexcept override;
		void newsArticle( int requestId, int articleType, const std::string& articleText )noexcept override;
		void historicalNews( int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline )noexcept override;
		void historicalNewsEnd( int requestId, bool hasMore )noexcept override;

	private:
		//sp<vector<Proto::Results::OptionParams>> securityDefinitionOptionalParameterEndSync( int reqId )noexcept override;
		void securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept override;
		void HandleBadTicker( TickerId ibReqId )noexcept;
		WrapperWeb()noexcept(false);
		static sp<WrapperWeb> _pInstance;

		map<TickerId,up<Proto::Results::HistoricalData>> _historicalData; mutex _historicalDataMutex;
		const map<string,string> _accounts;
		UnorderedSet<TickerId> _canceledItems;
		flat_map<TickerId,Proto::Results::HistoricalNewsCollection*> _allocatedNews;  mutex _newsMutex;
	};
}