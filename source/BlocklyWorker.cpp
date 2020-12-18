#include "BlocklyWorker.h"
#include "../../Blockly/source/Blockly.h"
#include "WebSendGateway.h"

#define var const auto
namespace Jde::Markets::TwsWebSocket
{
	using Blockly::Proto::ERequestType;
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
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.empty() )
		{
			BlocklyQueueType dflt;
			if( auto v = _queue.TryPop(5s); v )
				HandleRequest( std::move(*v) );
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
				Blockly::Save( msg.save() );
				Send( std::move(x), [](auto& out){out.set_success(true);} );
			}
			else if( msg.has_copy() )
			{
				Blockly::Copy( msg.copy().from_id(), msg.copy().to() );
				Send( std::move(x), [](auto& out){out.set_success(true);} );
			}
			else if( msg.has_id_request() )
			{
				var type = msg.id_request().type(); var id = msg.id_request().id();
				THROW_IF( id.empty(), Exception("Request {}, passed empty id", id) );
				if( type==ERequestType::Load )
					Send( std::move(x), [p=Blockly::Load(id).release()](auto& out)mutable{out.set_allocated_function(p);} );
				else
				{
					if( type==ERequestType::Delete )
						Blockly::Delete( id );
					else if( type==ERequestType::Build )
						Blockly::Build( id );
					else if( type==ERequestType::DeleteBuild )
						Blockly::DeleteBuild( id );
					else if( type==ERequestType::Enable )
						Blockly::Enable( id );
					else if( type==ERequestType::Disable )
						Blockly::Disable( id );

					Send( std::move(x), [](auto& out){out.set_success(true);} );
				}
			}
			else
			{
				auto pFunctions = Blockly::Load();
				Send( std::move(x), [&pFunctions](auto& out){out.set_allocated_functions(pFunctions.release());} );
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