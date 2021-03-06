#pragma once
#include "../../Framework/source/threading/InterruptibleThread.h"

namespace Jde::Markets{ struct TwsClientSync; }
namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway; struct ClientKey; struct WrapperWeb;
	struct TwsSendWorker
	{
		TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept;

		typedef tuple<SessionKey,sp<Proto::Requests::RequestUnion>> QueueType;

		void Push( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionInfo )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( sp<Proto::Requests::RequestUnion> pData, const SessionKey& key )noexcept;

		void AccountUpdates( const Proto::Requests::RequestAccountUpdates& request, const SessionKey& key )noexcept;
		void AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates, const SessionKey& key )noexcept;
		void CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept;
		void CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )noexcept(false);
		void ContractDetails( const Proto::Requests::RequestContractDetails& request, const SessionKey& key )noexcept;
		void Executions( const Proto::Requests::RequestExecutions& request, const SessionKey& key )noexcept;
		void HistoricalData( const Proto::Requests::RequestHistoricalData& req, const SessionKey& key )noexcept(false);
		void MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, const SessionKey& key )noexcept;
		void OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )noexcept;
		void Order( const Proto::Requests::PlaceOrder& order, const SessionKey& key )noexcept;
		void Positions( const Proto::Requests::RequestPositions& request, const SessionKey& key )noexcept;
		void RequestOptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )noexcept;

		::Contract GetContract( ContractPK contractId )noexcept(false);
		void Requests( const Proto::Requests::GenericRequests& request, const SessionKey& key )noexcept;
		//flat_map<ContractPK,TickerId> _mktData; mutable std::mutex _mktDataMutex;
		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
	};
}