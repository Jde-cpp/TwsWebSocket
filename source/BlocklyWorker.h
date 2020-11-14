#pragma once

namespace Jde::Blockly::Proto{ class ResultUnion; }
namespace Jde::Markets::TwsWebSocket
{
	struct WebSendGateway;
	struct BlocklyQueueType
	{
		SessionPK SessionId;
		ClientPK ClientId;
		up<string> MessagePtr;
	};
	struct BlocklyWorker
	{
		BlocklyWorker( sp<WebSendGateway> webSendPtr )noexcept;

		void Push( BlocklyQueueType&& x )noexcept;
		void Shutdown()noexcept{ _pThread->Interrupt(); _pThread->Join(); }
	private:
		void Run()noexcept;
		void HandleRequest( BlocklyQueueType&& x )noexcept;
		void Send( BlocklyQueueType&& x, function<void(Blockly::Proto::ResultUnion&)> set )noexcept;

		sp<Threading::InterruptibleThread> _pThread;
		QueueMove<BlocklyQueueType> _queue;
		sp<WebSendGateway> _webSendPtr;
	};
}