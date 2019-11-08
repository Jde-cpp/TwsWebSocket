#include "stdafx.h"
#include "WebSocket.h"
#include <EClient.h>
#include "WrapperWeb.h"
#include "EWebReceive.h"
#include "EWebSend.h"
#include "../../MarketLibrary/source/TwsClient.h"


#define var const auto

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

namespace Jde::Markets::TwsWebSocket
{
	shared_ptr<WebSocket> WebSocket::_pInstance{nullptr};
	WebSocket::WebSocket( uint16 port ):
		Threading::Interrupt( "webSend", 100ms, true ),
		_port{ port },
		_pAcceptor{ make_shared<Threading::InterruptibleThread>("wsAcceptor",[&](){Accept();}) }
	{
		Application::AddThread( _pAcceptor );
	}

	WebSocket& WebSocket::Create( uint16 port )noexcept
	{
		ASSERT( !_pInstance );
		_pInstance = shared_ptr<WebSocket>{ new WebSocket(port) };
		Application::AddShutdown( _pInstance );
		return *_pInstance;
	}

	WebSocket& WebSocket::Instance()noexcept
	{
		ASSERT( _pInstance );
		return *_pInstance;
	}
	void WebSocket::Shutdown()noexcept
	{
		DBG0( "WebSocket::Shutdown" );
		if( _pAcceptObject )
			_pAcceptObject->close();
		_pAcceptObject = nullptr;
		DBG0( "WebSocket::Shutdown - Leaving" );
	}
	std::once_flag SingleClient;
	void WebSocket::Accept()noexcept
	{
		Threading::SetThreadDescription( "wsAcceptor" );
		
		try
		{
			std::call_once( SingleClient, [&]()
			{ 
				TwsConnectionSettings settings; 
				from_json( Jde::Settings::Global().SubContainer("tws")->Json(), settings ); 
				WrapperWeb::CreateInstance( settings ); 
			});
		}
		catch( const Exception& e )
		{
			ERR0( e.what() );
			return;
		}

		boost::asio::io_context ioc{1};
		_pAcceptObject =  shared_ptr<tcp::acceptor>( new tcp::acceptor(ioc, {boost::asio::ip::tcp::v4(), (short unsigned int)_port}) );
		INFO( "Accepting web sockets on port {}.", _port );
		while( !Threading::GetThreadInterruptFlag().IsSet() )
		{
			try
			{
				tcp::socket socket{ioc};// This will receive the new connection
				_pAcceptObject->accept(socket);// Block until we get a connection
				DBG0( "Accepted Connection." );
				auto pSession = make_shared<websocket::stream<tcp::socket>>( std::move(socket) );
				pSession->binary( true );
				Application::AddThread( make_shared<Threading::InterruptibleThread>("WebSession", [&,pSession](){DoSession(pSession);}) );
			}
			catch( boost::system::system_error& e )
			{
				DBG( "Accept failed:  {}", e.what() );
			}
		}
		TwsProcessor::Stop();
		DBG0( "Leaving WebSocket::Accept()" );
	}

	void WebSocket::EraseSession( SessionId id )noexcept
	{
		DBG( "Removing session '{}'", id );
		_sessions.erase( id );
		std::function<void(const Proto::Results::EResults&, Collections::UnorderedSet<SessionId>& )> func = [&]( const Proto::Results::EResults& messageId, Collections::UnorderedSet<SessionId>& sessions )
		{
			sessions.EraseIf( [&](const SessionId& id){return !_sessions.Find(id);} );//if lost others.
			if( messageId==Proto::Results::EResults::PositionData )
			{
				sessions.IfEmpty( [&]()
				{
					//WrapperWeb::Instance().TwsClient().cancelPositions();
					_requests.erase( Proto::Requests::ERequests::Positions );
				});
			}
		};
		_requestSessions.ForEach( func );
		std::unique_lock<std::shared_mutex> l( _accountRequestMutex );
		for( auto pAccountSessionIds = _accountRequests.begin(); pAccountSessionIds!=_accountRequests.end();  )
		{
			var& accountNumber = pAccountSessionIds->first;
			auto& sessionIds =   pAccountSessionIds->second;
			if( sessionIds.erase(id) && sessionIds.size()==0 )
			{
				TwsClient::Instance().reqAccountUpdates( false, accountNumber );
				pAccountSessionIds = _accountRequests.erase( pAccountSessionIds );
			}
			else
				++pAccountSessionIds;
		}
	}
}