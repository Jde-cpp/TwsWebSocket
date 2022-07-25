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
		TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )ι;

		using QueueType=tuple<SessionKey,sp<Proto::Requests::RequestUnion>>;
		α Push( sp<Proto::Requests::RequestUnion> pData, const SessionKey& sessionInfo )ι->void;
		α Shutdown()ι{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		α Run()ι->void;
		α HandleRequest( sp<Proto::Requests::RequestUnion> pData, const SessionKey& key )ι->void;

		α AccountUpdates( const Proto::Requests::RequestAccountUpdates& request, const SessionKey& key )ι->void;
		α AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates, const SessionKey& key )ι->void;
		α CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )ι->void;
		α CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )ε->void;
		α Executions( const Proto::Requests::RequestExecutions& request, const SessionKey& key )ι->void;
		α HistoricalData( Proto::Requests::RequestHistoricalData req, SessionKey key )ι->Task;
		α MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, const SessionKey& key )ι->void;
		α OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const SessionKey& key )ι->void;
		α Positions( const Proto::Requests::RequestPositions& request, const SessionKey& key )ι->void;
		α RequestOptionParams( google::protobuf::int32 underlyingId, ClientKey c )ι->Task;
		α RequestAllOpenOrders( ClientKey client )ι->Task;
		α GetContract( ContractPK contractId )ε->const ::Contract&;
		α Requests( const Proto::Requests::GenericRequests& request, const SessionKey& key )ι->void;
		α Request( const Proto::Requests::GenericRequest& r, const SessionKey& key )ι->void;

		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
	};
}