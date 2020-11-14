#include "BlocklyWorker.h"
#include "../../Blockly/source/Blockly.h"
#include "WebSendGateway.h"

#define var const auto
namespace Jde::Markets::TwsWebSocket
{
	fs::path Path()
	{
		return IApplication::Instance().ApplicationDataFolder()/"blockly";
	}
	BlocklyWorker::BlocklyWorker( sp<WebSendGateway> webSendPtr )noexcept:
		_queue{},
		_webSendPtr{ webSendPtr }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "BlocklyWorker", [&](){Run();} );
	}

	void BlocklyWorker::Push( BlocklyQueueType&& x )noexcept
	{
		_queue.Push( std::move(x) );
	}

	void BlocklyWorker::Run()noexcept
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.Empty() )
		{
			BlocklyQueueType dflt;
			if( _queue.TryPop(dflt, 5s) )
				HandleRequest( std::move(dflt) );
		}
	}
	void BlocklyWorker::Send( BlocklyQueueType&& x, function<void(Blockly::Proto::ResultUnion&)> set )noexcept
	{
		Blockly::Proto::ResultUnion output;
		set( output );
		string bytes;
		if( !output.SerializeToString(&bytes) )
			ERR0( "output.SerializeToString(&bytes) returned false"sv );
		auto pCustom = make_unique<Proto::Results::Custom>(); pCustom->set_request_id( x.ClientId ); pCustom->set_message( bytes );
		auto pResult = make_shared<MessageType>(); pResult->set_allocated_custom( pCustom.release() );
		_webSendPtr->Push( pResult, x.SessionId );
	}
	void BlocklyWorker::HandleRequest( BlocklyQueueType&& x )noexcept
	{
		try
		{
			Blockly::Proto::RequestUnion msg;
			if( google::protobuf::io::CodedInputStream stream{reinterpret_cast<const unsigned char*>(x.MessagePtr->data()), (int)x.MessagePtr->size()}; !msg.MergePartialFromCodedStream(&stream) )
				THROW( IOException("transmission.MergePartialFromCodedStream returned false") );
			if( msg.has_save() )
			{
				Blockly::Save( Path(), msg.save() );
				Send( std::move(x), [](auto& out){out.set_success(true);} );
			}
			else if( msg.delete_id().size() )
			{
				Blockly::Delete( Path(), msg.delete_id() );
				Send( std::move(x), [](auto& out){out.set_success(true);} );
			}
			else
			{
				var& id = msg.load_id();
				if( id.size() )
				{
					auto pFunction = Blockly::Load( Path(), id );
					Send( std::move(x), [&pFunction](auto& out){out.set_allocated_function(pFunction.release());} );
				}
				else
				{
					auto pFunctions = Blockly::Load( Path() );
					Send( std::move(x), [&pFunctions](auto& out){out.set_allocated_functions(pFunctions.release());} );
				}
			}
		}
		catch( const IOException& e )
		{
			DBG( "IOExeption returned: '{}'"sv, e.what() );
			_webSendPtr->Push( e, {x.SessionId, x.ClientId} );
		}
		catch( const Exception& e )
		{
			DBG( "Exeption returned: '{}'"sv, e.what() );
			_webSendPtr->Push( e, {x.SessionId, x.ClientId} );
		}
	}
}