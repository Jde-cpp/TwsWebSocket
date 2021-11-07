#pragma once
#include <jde/coroutine/Task.h>
#include "../../Framework/source/threading/InterruptibleThread.h"


namespace Jde::Markets{ struct TwsClientSync; }
namespace Jde::Markets::TwsWebSocket
{
	using namespace Jde::Coroutine;
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
		α HistoricalData( Proto::Requests::RequestHistoricalData&& req, SessionKey key )noexcept(false)->Task2;
		void MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, const SessionKey& key )noexcept;
		void OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )noexcept;
		void Order( const Proto::Requests::PlaceOrder& order, const SessionKey& key )noexcept;
		void Positions( const Proto::Requests::RequestPositions& request, const SessionKey& key )noexcept;
		α RequestOptionParams( ClientPK reqId, google::protobuf::int32 underlyingId, SessionKey key )noexcept->Task2;

		::Contract GetContract( ContractPK contractId )noexcept(false);
		α Requests( const Proto::Requests::GenericRequests& request, const SessionKey& key )noexcept->void;
		α Request( const Proto::Requests::GenericRequest& r, const SessionKey& key )noexcept->void;
		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
	};
}