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

		using QueueType=tuple<SessionKey,sp<Proto::Requests::RequestUnion>>;
		α Push( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionInfo )noexcept->void;
		α Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		α Run()noexcept->void;
		α HandleRequest( sp<Proto::Requests::RequestUnion> pData, const SessionKey& key )noexcept->void;

		α AccountUpdates( const Proto::Requests::RequestAccountUpdates& request, const SessionKey& key )noexcept->void;
		α AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates, const SessionKey& key )noexcept->void;
		α CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept->void;
		α CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )noexcept(false)->void;
		α ContractDetails( const Proto::Requests::RequestContractDetails& request, const SessionKey& key )noexcept->void;
		α Executions( const Proto::Requests::RequestExecutions& request, const SessionKey& key )noexcept->void;
		α HistoricalData( Proto::Requests::RequestHistoricalData req, SessionKey key )noexcept(false)->Task2;
		α MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, const SessionKey& key )noexcept->void;
		α OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )noexcept->void;
		α Order( const Proto::Requests::PlaceOrder& order, const SessionKey& key )noexcept->void;
		α Positions( const Proto::Requests::RequestPositions& request, const SessionKey& key )noexcept->void;
		α RequestOptionParams( ClientPK reqId, google::protobuf::int32 underlyingId, SessionKey key )noexcept->Task2;
		α RequestAllOpenOrders( ClientKey client )noexcept->Task2;
		α GetContract( ContractPK contractId )noexcept(false)->::Contract;
		α Requests( const Proto::Requests::GenericRequests& request, const SessionKey& key )noexcept->void;
		α Request( const Proto::Requests::GenericRequest& r, const SessionKey& key )noexcept->void;

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
	};
}