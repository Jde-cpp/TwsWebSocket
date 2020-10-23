#pragma once

namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway; struct ClientKey; struct WrapperWeb;
	struct TwsSendWorker
	{
		TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept;

		typedef tuple<SessionId,sp<Proto::Requests::RequestUnion>> QueueType;

		void Push( sp<Proto::Requests::RequestUnion> pData, SessionId sessionId )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( sp<Proto::Requests::RequestUnion> pData, SessionId sessionId )noexcept;

		void AccountUpdates( const Proto::Requests::RequestAccountUpdates& request, SessionId sessionId )noexcept;
		void AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates, SessionId sessionId )noexcept;
		void CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept;
		void CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )noexcept(false);
		void ContractDetails( const Proto::Requests::RequestContractDetails& request, SessionId sessionId )noexcept;
		void Executions( const Proto::Requests::RequestExecutions& request, SessionId sessionId )noexcept;
		void HistoricalData( const Proto::Requests::RequestHistoricalData& req, SessionId sessionId )noexcept(false);
		void MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, SessionId sessionId )noexcept;
		void OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, SessionId sessionId )noexcept;
		void Order( const Proto::Requests::PlaceOrder& order, SessionId sessionId )noexcept;
		void Positions( const Proto::Requests::RequestPositions& request, SessionId sessionId )noexcept;
		void RequestOptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept;

		::Contract GetContract( ContractPK contractId )noexcept(false);
		void Requests( const Proto::Requests::GenericRequests& request, SessionId sessionId )noexcept;
		flat_map<ContractPK,TickerId> _mktData; mutable std::mutex _mktDataMutex;
		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
		//sp<WrapperWeb> _wrapperPtr;
	};
}