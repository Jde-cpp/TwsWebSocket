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

		void accountUpdateMulti( int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency )noexcept override;
		void accountUpdateMultiEnd( int reqId )noexcept override;
		void contractDetails( int reqId, const ibapi::ContractDetails& contractDetails)noexcept override;
		void contractDetailsEnd( int reqId)noexcept override;
		void historicalData( TickerId reqId, const ibapi::Bar& bar )noexcept override;
		void historicalDataEnd( int reqId, const std::string& startDateStr, const std::string& endDateStr )noexcept override;
		void error(int id, int errorCode, const std::string& errorString)noexcept override;
		void managedAccounts( const std::string& accountsList)noexcept override;
		void nextValidId( ibapi::OrderId orderId )noexcept override;
		void orderStatus( ibapi::OrderId orderId, const std::string& status, double filled,	double remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice)noexcept override;
		void openOrder( ibapi::OrderId orderId, const ibapi::Contract&, const ibapi::Order&, const ibapi::OrderState&)noexcept override;
		void openOrderEnd()noexcept override;

		void tickPrice( TickerId tickerId, TickType field, double price, const TickAttrib& attrib)noexcept override;
		void tickSize( TickerId tickerId, TickType field, int size)noexcept override;
		void tickGeneric(TickerId tickerId, TickType tickType, double value)noexcept override;
		void tickString(TickerId tickerId, TickType tickType, const std::string& value)noexcept override;
		void tickSnapshotEnd( int reqId)noexcept override;

		//need to cache values between calls.
		void updateAccountValue( const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName )noexcept override;
		void updatePortfolio( const ibapi::Contract& contract, double position, double marketPrice, double marketValue, double /*averageCost*/, double /*unrealizedPNL*/, double /*realizedPNL*/, const std::string& /*accountName*/)noexcept  override;
		void updateAccountTime( const std::string& timeStamp )noexcept override;

		bool AddCanceled( TickerId id )noexcept{ return _canceledItems.emplace(id); }
	private:
		//sp<vector<Proto::Results::OptionParams>> securityDefinitionOptionalParameterEndSync( int reqId )noexcept override;
		void securityDefinitionOptionalParameter( int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes )noexcept override;
		void HandleBadTicker( TickerId ibReqId )noexcept;
		WrapperWeb()noexcept(false);
		static sp<WrapperWeb> _pInstance;

		shared_ptr<EReaderSignal> _pReaderSignal;
		map<TickerId,up<Proto::Results::HistoricalData>> _historicalData; mutex _historicalDataMutex;
		const map<string,string> _accounts;
		UnorderedSet<TickerId> _canceledItems;
	};
}