#pragma once

namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway; struct ClientKey; struct WrapperWeb;
	struct TwsSendWorker
	{
		TwsSendWorker( sp<WebSendGateway> webSendPtr, sp<TwsClientSync> pTwsClient )noexcept;

		typedef tuple<SessionPK,sp<Proto::Requests::RequestUnion>> QueueType;

		void Push( sp<Proto::Requests::RequestUnion> pData, SessionPK sessionId )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( sp<Proto::Requests::RequestUnion> pData, SessionPK sessionId )noexcept;

		void AccountUpdates( const Proto::Requests::RequestAccountUpdates& request, SessionPK sessionId )noexcept;
		void AccountUpdatesMulti( const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates, SessionPK sessionId )noexcept;
		void CalculateImpliedPrice( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedPrice>& requests, const ClientKey& client )noexcept;
		void CalculateImpliedVolatility( const google::protobuf::RepeatedPtrField<Proto::Requests::ImpliedVolatility>& requests, const ClientKey& client )noexcept(false);
		void ContractDetails( const Proto::Requests::RequestContractDetails& request, SessionPK sessionId )noexcept;
		void Executions( const Proto::Requests::RequestExecutions& request, SessionPK sessionId )noexcept;
		void HistoricalData( const Proto::Requests::RequestHistoricalData& req, SessionPK sessionId )noexcept(false);
		void MarketDataSmart( const Proto::Requests::RequestMrkDataSmart& request, SessionPK sessionId )noexcept;
		void OptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, SessionPK sessionId )noexcept;
		void Order( const Proto::Requests::PlaceOrder& order, SessionPK sessionId )noexcept;
		void Positions( const Proto::Requests::RequestPositions& request, SessionPK sessionId )noexcept;
		void RequestOptionParams( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept;

		::Contract GetContract( ContractPK contractId )noexcept(false);
		void Requests( const Proto::Requests::GenericRequests& request, SessionPK sessionId )noexcept;
		//flat_map<ContractPK,TickerId> _mktData; mutable std::mutex _mktDataMutex;
		sp<Threading::InterruptibleThread> _pThread;
		QueueValue<QueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
		sp<TwsClientSync> _twsPtr;
		//sp<WrapperWeb> _wrapperPtr;
	};
}