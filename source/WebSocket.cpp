#include "WebSocket.h"
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <EClient.h>
#include "WrapperWeb.h"
#include "EWebReceive.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"


#define var const auto
#define _client TwsClientSync::Instance()

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>
namespace Jde::Markets::TwsWebSocket
{
	shared_ptr<WebSocket> WebSocket::_pInstance{nullptr};
	WebSocket::WebSocket( uint16 port, sp<TwsClientSync> pClient )noexcept:
		Threading::Interrupt( "webSocket", 100ms, true ),
		_port{ port },
		_pClientSync{ pClient },
	//	_pWebSend{ make_shared<WebSendGateway>(*this, _pClientSync) },
		_requestWorker{ /**this,*/ _pWebSend, _pClientSync }
	{
		_pAcceptor = make_shared<Threading::InterruptibleThread>( "wsAcceptor", [&](){Accept();} );//accesses members, so not initialized above.
		IApplication::AddThread( _pAcceptor );
	}

	WebSocket& WebSocket::Create( uint16 port, sp<TwsClientSync> pClient )noexcept
	{
		ASSERT( !_pInstance );
		_pInstance = shared_ptr<WebSocket>{ new WebSocket(port, pClient) };
		IApplication::AddShutdown( _pInstance );
		return *_pInstance;
	}

	WebSocket& WebSocket::Instance()noexcept
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}
	void WebSocket::Shutdown()noexcept
	{
		DBG0( "WebSocket::Shutdown"sv );
		if( _pAcceptObject )
			_pAcceptObject->close();
		_pAcceptObject = nullptr;
		_requestWorker.Shutdown();
		_pWebSend->Shutdown();
		_pWebSend = nullptr;
		DBG0( "WebSocket::Shutdown - Leaving"sv );
	}

	std::once_flag SingleClient;
	void WebSocket::Accept()noexcept
	{
		Threading::SetThreadDscrptn( "webAcceptor" );

		boost::asio::io_context ioc{1};
		try
		{
			_pAcceptObject =  shared_ptr<tcp::acceptor>( new tcp::acceptor{ioc, {boost::asio::ip::tcp::v4(), (short unsigned int)_port}} );
		}
		catch( boost::system::system_error& e )
		{
			ERR( "Could not accept web sockets - {}"sv, e.what() );
			return;
		}
		INFO( "Accepting web sockets on port {}."sv, _port );
		while( !Threading::GetThreadInterruptFlag().IsSet() )
		{
			try
			{
				tcp::socket socket{ioc};// This will receive the new connection
				_pAcceptObject->accept(socket);// Block until we get a connection
				DBG0( "Accepted Connection."sv );
				auto pSession = make_shared<websocket::stream<tcp::socket>>( std::move(socket) );
				pSession->binary( true );
				IApplication::AddThread( make_shared<Threading::InterruptibleThread>("WebSession", [&,pSession](){DoSession(pSession);}) );
			}
			catch( boost::system::system_error& e )
			{
				DBG( "Accept failed:  {}"sv, e.what() );
			}
		}
		TwsProcessor::Stop();
		DBG0( "Leaving WebSocket::Accept()"sv );
	}

	void WebSocket::DoSession( shared_ptr<Stream> pSession )noexcept
	{
		var sessionId = ++_sessionId;
		{
			Threading::SetThreadDscrptn( format("webReceive - {}", sessionId) );
			try
			{
				pSession->accept();
				_sessions.emplace( sessionId, pSession );
			}
			catch( const boost::system::system_error& se )
			{
				if( se.code()==websocket::error::closed )
					DBG( "Socket Closed on session {} when sending acceptance."sv, sessionId );
				if( se.code() != websocket::error::closed )
					ERR( "Error sending app - {}."sv, se.code().message() );
			}
			catch( const std::exception& e )
			{
				ERR0( string(e.what()) );
			}
		}
		if( !_sessions.Find(sessionId) )
			return;
		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_type( Proto::Results::EResults::Accept ); pMessage->set_int_value( sessionId );
		auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_message( pMessage );
		_pWebSend->Push( pMessageUnion, sessionId );
		DBG( "Listening to session #{}"sv, sessionId );
		UnPause();
		while( !Threading::GetThreadInterruptFlag().IsSet() )
		{
			try
			{
				boost::beast::multi_buffer buffers;
				pSession->read( buffers );
				auto data = boost::beast::buffers_to_string( buffers.data() );
				_requestWorker.Push( sessionId, std::move(data) );
			}
			catch(boost::system::system_error const& se)
			{
				auto code = se.code();
				if(  code == websocket::error::closed )
					DBG( "se.code()==websocket::error::closed, id={}"sv, sessionId );
				else
				{
					if( code.value()==104 )//reset by peer
						DBG( "system_error returned: '{}' - closing connection - {}"sv, se.code().message(), sessionId );
					else
						DBG( "system_error returned: '{}' - closing connection - {}"sv, se.code().message(), sessionId );
					EraseSession( sessionId );
					break;
				}
			}
			catch( std::exception const& e )
			{
				ERR( "std::exception returned: '{}'"sv, e.what() );
			}
			if( !_sessions.Find(sessionId) )
			{
				DBG( "Could not find session id {} exiting thread."sv, sessionId );
				break;
			}
		}
		DBG0( "Leaving WebSocket::DoSession"sv );
	}

	void WebSocket::AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [pUnion]( Queue<MessageType>& queue ){ queue.Push( pUnion ); };
		_outgoing.Insert( afterInsert, id, sp<Queue<MessageType>>{new Queue<MessageType>()} );
	}
	void WebSocket::AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [&messages]( Queue<MessageType>& queue ){ for_each(messages.begin(), messages.end(), [&queue](auto& m){queue.Push( m );} ); };
		_outgoing.Insert( afterInsert, id, sp<Queue<MessageType>>{new Queue<MessageType>()} );
	}
	void WebSocket::AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [&messages]( Queue<MessageType>& queue )
		{
			for_each( messages.begin(), messages.end(), [&queue](auto& m){queue.Push( make_shared<MessageType>(move(m)) );} );
		};
		_outgoing.Insert( afterInsert, id, make_shared<Queue<MessageType>>() );
	}
	void WebSocket::OnTimeout()noexcept//TODO fire when queue has items, not on timeout.
	{
		if( !_outgoing.size() )
			return;

		map<SessionPK,vector<char>> buffers;
		std::function<void(const SessionPK&, Queue<MessageType>&)> createBuffers = [&buffers]( const SessionPK& id, Queue<MessageType>& queue )->void
		{
			if( !queue.size() )
				return;

			Proto::Results::Transmission transmission;
			std::function<void(MessageType&)> getFunction = [&transmission]( MessageType& message )
			{
				auto pNewMessage = transmission.add_messages();
				*pNewMessage = message;
			};
			queue.ForEach( getFunction );
			const size_t size = transmission.ByteSizeLong();
			auto& buffer = buffers.emplace( id, std::vector<char>(size) ).first->second;
			transmission.SerializeToArray( buffer.data(), (int)buffer.size() );
		};
		_outgoing.ForEach( createBuffers );
		if( !buffers.size() )
			DBG0( "!buffers.size()"sv );
		_outgoing.eraseIf( [](const Queue<MessageType>& queue){ return queue.size()==0; } );
		set<SessionPK> brokenIds;
		for( var& [id, buffer] : buffers )
		{
			auto pSession = _sessions.Find( id );
			if( !pSession )
			{
				_outgoing.erase( id );
				continue;
			}
			try
			{
				pSession->write( boost::asio::buffer(buffer.data(), buffer.size()) );
			}
			catch( boost::exception& e )
			{
				brokenIds.emplace( id ); ERRN( "removing because Error writing to Session:  {}", boost::diagnostic_information(&e) );
				try
				{
					pSession->close( websocket::close_code::none );
				}
				catch( const boost::exception& e2 )	{ERRN( "Error closing:  ", boost::diagnostic_information(&e2) );}
			}
		};
		for( var id : brokenIds )
			EraseSession( id );
	}

	void WebSocket::EraseSession( SessionPK id )noexcept
	{
		DBG( "Removing session '{}'"sv, id );
		_sessions.erase( id );
		_pWebSend->EraseSession( id );
	}
}