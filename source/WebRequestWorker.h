#pragma once
//#include "./TypeDefs.h"
#include "./TwsSendWorker.h"
#include "./WebSendGateway.h"

namespace Jde::Markets::TwsWebSocket
{
//	struct WebSendGateway;
	struct ProcessArg : ClientKey
	{
		ProcessArg( const ClientKey& key, sp<WebSendGateway> webSendPtr ): ClientKey{ key }, WebSendPtr{ webSendPtr } {}
		ProcessArg():ClientKey{0,0}{}
		TickerId AddRequestSession()const noexcept{ return WebSendPtr->AddRequestSession( *this ); }
		void Push( sp<MessageType> pUnion )const noexcept{ WebSendPtr->Push(pUnion, SessionId); }
		void Push( const Exception& e )const noexcept{ WebSendPtr->Push(e, *this); }
		void Push( EResults x )const noexcept{ WebSendPtr->Push(x, *this); }
		sp<WebSendGateway> WebSendPtr;
	};
	class WebSocket;
	struct BlocklyWorker;
	struct WebRequestWorker
	{
		typedef tuple<SessionPK,string> QueueType;
		WebRequestWorker( /*WebSocket& webSocketParent,*/ sp<WebSendGateway> webSend, sp<TwsClientSync> pTwsClient )noexcept;
		void Push( SessionPK sessionId, string&& data )noexcept;
		void Shutdown()noexcept{ _pTwsSend->Shutdown(); _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( SessionPK sessionId, string&& data )noexcept;

		bool ReceiveRequests( SessionPK sessionId, const Proto::Requests::GenericRequests& request )noexcept;
		void ReceiveStdDev( ContractPK contractId, double days, DayIndex start, const ProcessArg& inputArg )noexcept;
		void Receive( Proto::Requests::ERequests type, const string& name, const ClientKey& arg )noexcept;
		void ReceiveFlex( SessionPK sessionId, const Proto::Requests::FlexExecutions& req )noexcept;
		void ReceiveOptions( SessionPK sessionId, const Proto::Requests::RequestOptions& request )noexcept;
		void RequestFundamentalData( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept;

		sp<Threading::InterruptibleThread> _pThread;
		QueueMove<QueueType> _queue;
		sp<TwsSendWorker> _pTwsSend;//needs to be pointer, gets passed to other threads.
		sp<WebSendGateway> _pWebSend;//needs to be pointer, gets passed to other threads.
		sp<BlocklyWorker> _pBlocklyWorker;//needs to be pointer, gets passed to other threads.
	};
}