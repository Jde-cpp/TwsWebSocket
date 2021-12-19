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
		α AddRequestSession()const noexcept{ return WebSendPtr->AddRequestSession( *this ); }
		β Push( MessageType&& m )const noexcept(false)->void{ WebSendPtr->Push( move(m), SessionId); }
		α Push( string m, const IException& e )const noexcept{ WebSendPtr->Push( move(m), e, *this ); }
		β PushError( string m )const noexcept->void{ WebSendPtr->PushError( move(m), *this ); }
		α Push( EResults x )const noexcept{ WebSendPtr->Push(x, *this); }
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
		α Push( QueueType&& msg )noexcept->void;
		α Shutdown()noexcept{ _pTwsSend->Shutdown(); _pThread->Interrupt(); _pThread->Join(); }
	private:
		α Run()noexcept->void;
		α HandleRequest( QueueType&& msg )noexcept->void;
		α HandleRequest( Proto::Requests::RequestTransmission&& transmission, SessionKey&& session )noexcept->void;
		α ReceiveRequests( const SessionKey& session, const Proto::Requests::GenericRequests& request )noexcept->bool;
		α ReceiveStdDev( ContractPK contractId, double days, DayIndex start, ProcessArg inputArg )noexcept->Task2;
		α Receive( Proto::Requests::ERequests type, string&& name, const ClientKey& arg )noexcept->void;
		α ReceiveFlex( const SessionKey& session, const Proto::Requests::FlexExecutions& req )noexcept->void;
		α ReceiveOptions( const SessionKey& session, const Proto::Requests::RequestOptions& request )noexcept->void;
		α RequestFundamentalData( const google::protobuf::RepeatedField<google::protobuf::int32>& underlyingIds, const ClientKey& key )noexcept->void;
		α ReceiveRest( const ClientKey& arg, Proto::Requests::ERequests type, sv url, sv item )noexcept->void;
		sp<Threading::InterruptibleThread> _pThread;
		QueueMove<QueueType> _queue;
		sp<TwsSendWorker> _pTwsSend;//needs to be pointer, gets passed to other threads.
		sp<WebSendGateway> _pWebSend;//needs to be pointer, gets passed to other threads.
		sp<BlocklyWorker> _pBlocklyWorker;//needs to be pointer, gets passed to other threads.
	};
}