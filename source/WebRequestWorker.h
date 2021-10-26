#pragma once
#include "./TwsSendWorker.h"
#include "./WebSendGateway.h"

namespace Jde::Markets::TwsWebSocket
{
//	struct WebSendGateway;
	struct ProcessArg /*final*/: ClientKey
	{
		ProcessArg( const ClientKey& key, sp<WebSendGateway> webSendPtr ): ClientKey{ key }, WebSendPtr{ webSendPtr } {}
		ProcessArg( const SessionKey& key, ClientPK clientId, sp<WebSendGateway> webSendPtr ): ClientKey{ key, clientId }, WebSendPtr{ webSendPtr } {}
		ProcessArg()=default;
		TickerId AddRequestSession()const noexcept{ return WebSendPtr->AddRequestSession( *this ); }
		virtual void Push( MessageType&& m )const noexcept(false){ WebSendPtr->Push( move(m), SessionId); }
		//virtual void Push( const IException& e )const noexcept{ TRY(WebSendPtr->Push(e, *this)); }
		void Push( const std::exception& e )const noexcept{ TRY(WebSendPtr->Push(e, *this)); }
		void Push( EResults x )const noexcept{ WebSendPtr->Push(x, *this); }
		sp<WebSendGateway> WebSendPtr;
	};
	class WebSocket;
	struct BlocklyWorker;
	struct WebRequestMessage : SessionKey
	{
		WebRequestMessage( SessionKey key, string&& data ):SessionKey{key},Data{move(data)}{}
		string Data;
	};
	struct WebRequestWorker
	{
		typedef WebRequestMessage QueueType;
		WebRequestWorker( /*WebSocket& webSocketParent,*/ sp<WebSendGateway> webSend, sp<TwsClientSync> pTwsClient )noexcept;
		void Push( QueueType&& msg )noexcept;
		void Shutdown()noexcept{ _pTwsSend->Shutdown(); _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( QueueType&& msg )noexcept;
		void HandleRequest( Proto::Requests::RequestTransmission&& transmission, SessionKey&& session )noexcept;
		bool ReceiveRequests( const SessionKey& session, const Proto::Requests::GenericRequests& request )noexcept;
		void ReceiveStdDev( ContractPK contractId, double days, DayIndex start, const ProcessArg& inputArg )noexcept;
		void Receive( Proto::Requests::ERequests type, str name, const ClientKey& arg )noexcept;
		void ReceiveFlex( const SessionKey& session, const Proto::Requests::FlexExecutions& req )noexcept;
		void ReceiveOptions( const SessionKey& session, const Proto::Requests::RequestOptions& request )noexcept;
		void RequestFundamentalData( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept;
		void ReceiveRest( const ClientKey& arg, Proto::Requests::ERequests type, sv url, sv item )noexcept;
		sp<Threading::InterruptibleThread> _pThread;
		QueueMove<QueueType> _queue;
		sp<TwsSendWorker> _pTwsSend;//needs to be pointer, gets passed to other threads.
		sp<WebSendGateway> _pWebSend;//needs to be pointer, gets passed to other threads.
		sp<BlocklyWorker> _pBlocklyWorker;//needs to be pointer, gets passed to other threads.
	};
}