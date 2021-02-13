#include "WebCoSocket.h"
#include "server_certificate.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/spawn.hpp>

//https://www.boost.org/doc/libs/develop/libs/beast/example/websocket/server/coro-ssl/websocket_server_coro_ssl.cpp
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	BeastException::BeastException( sv what, beast::error_code&& ec )noexcept:
		Exception{ "{} returned {}", what, ec.message() },
		ErrorCode{ move(ec) }
	{}

	void BeastException::LogCode( sv what, const beast::error_code& ec )noexcept
	{
		if( BeastException::IsTruncated(ec) )
			DBG( "async_accept Truncated - {}"sv, ec.message() );
		else
			WARN( "async_accept Failed - {}"sv, ec.message() );
	}

	sp<WebCoSocket> WebCoSocket::_pInstance;
	flat_map<SessionPK,SessionInfo> WebCoSocket::_sessions; shared_mutex WebCoSocket::_sessionMutex;
	uint WebCoSocket::_sessionId{0};
	sp<WebCoSocket> WebCoSocket::Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept
	{
		ASSERT( !_pInstance );
		return _pInstance = sp<WebCoSocket>( new WebCoSocket(settings, pClient) );
	}
	WebCoSocket::WebCoSocket( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept:
		_threadCount{ settings.Get2<uint8>("threadCount").value_or(1) },
		_port{ settings.Get2<uint16_t>("port").value_or((uint16)6812) },
		_pWebSend{ make_shared<WebSendGateway>(*this, pClient) },
		_requestWorker{ /**this,*/ _pWebSend, pClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebCoSocket", [&](){Run();} );
	}

	SessionPK WebCoSocket::AddConnection( sp<SocketStream> stream )noexcept
	{
		var sessionId = ++_sessionId;
		unique_lock l{ _sessionMutex };
		_sessions.emplace( sessionId, SessionInfo{stream,make_shared<std::atomic_bool>(false)} );
		return sessionId;
	}

	void WebCoSocket::AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false)
	{
		AddOutgoing( vector<MessageType>{move(msg)}, id );
/**/
	}

	void WebCoSocket::AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept
	{
		ASSERT( false );
	}
	void WebCoSocket::AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept
	{
		ASSERT( false );
	}

	void WebCoSocket::AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept(false)
	{
		Proto::Results::Transmission transmission;
		for( auto&& msg : messages )
			*transmission.add_messages() = move( msg );
		const size_t size = transmission.ByteSizeLong();
		auto pBuffer = make_shared<std::vector<char>>( size );
		transmission.SerializeToArray( pBuffer->data(), (int)pBuffer->size() );

		sp<SocketStream> pStream; sp<atomic<bool>> pMutex;
		{
			shared_lock l{ _sessionMutex };
			var p = _sessions.find( id ); THROW_IF( p==_sessions.end(), Exception("({})Could not find session."sv, id) );
			pStream = p->second.StreamPtr;
			pMutex = p->second.WriteLockPtr;
		}
		auto handler = [size, id, pMutex, pBuffer]( const boost::system::error_code& ec, size_t bytesTransferred )noexcept
		{
			*pMutex = false;
			if( size!=bytesTransferred )
				DBG( "({})size({})!=bytesTransferred({})"sv, id, size, bytesTransferred );
			if( ec )
			{
				BeastException::LogCode( format("({})async_write - killing session"sv, id), ec );
				unique_lock l{ _sessionMutex };
				_sessions.erase( id );
			}
		};
		while( pMutex->exchange(true) )//TODO This function shouldn't lock.  async_write only wants one write at a time
			std::this_thread::yield();
		pStream->async_write( boost::asio::buffer(pBuffer->data(), pBuffer->size()), handler );
	}

	UserPK WebCoSocket::UserId( SessionPK sessionId )noexcept(false)
	{
		shared_lock l{ _sessionMutex };
		var p = _sessions.find( sessionId ); THROW_IF( p==_sessions.end(), Exception("({})Could not find session", sessionId) );
		return p->second.UserId;
	}

	string _host;
	void DoSession( sp<SocketStream> pStream, net::yield_context yld )
	{
		Threading::SetThreadDscrptn( "WebSession" );
		beast::error_code ec;
		//beast::get_lowest_layer(*pStream).expires_after( 30s );
		try
		{
			// Set SNI Hostname (many hosts need this to handshake successfully)
			//THROW_IF( !SSL_set_tlsext_host_name(pStream->next_layer().native_handle(), _host.c_str()), boost::system::system_error(boost::system::error_code((int)ERR_get_error(), boost::asio::error::get_ssl_category())) );
			//pStream->next_layer().async_handshake( ssl::stream_base::server, yld[ec] );
#ifdef HTTPS
			pStream->next_layer().handshake( ssl::stream_base::server, ec );
#endif
			//THROW_IF( ec, BeastException("handshake", move(ec)) );
			beast::get_lowest_layer(*pStream).expires_never();		// Turn off the timeout on the tcp_stream, because the websocket stream has its own timeout system.
			pStream->set_option( websocket::stream_base::timeout::suggested(beast::role_type::server) );// Set suggested timeout settings for the websocket
			pStream->set_option( websocket::stream_base::decorator( [](websocket::response_type& res)  // Set a decorator to change the Server of the handshake
			{
				res.set( http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-coro-ssl" );
			}) );
			pStream->async_accept( yld[ec] ); THROW_IF( ec, BeastException("accept", move(ec)) );
			auto pWebCoSocket = WebCoSocket::Instance(); THROW_IF( !pWebCoSocket, Exception("!pWebCoSocket") );
			var sessionId = pWebCoSocket->AddConnection( pStream );
			UserPK userId = 0;
			for( ;; )
			{
				beast::flat_buffer buffer;// This buffer will hold the incoming message
				pStream->async_read( buffer, yld[ec] ); THROW_IF( ec, BeastException("read", move(ec)) );// Read a message
				//if( ec == websocket::error::closed )// This indicates that the session was closed
				//	break;
				auto data = boost::beast::buffers_to_string( buffer.data() );
				if( !userId )
					userId = pWebCoSocket->UserId( sessionId );

				pWebCoSocket->HandleIncoming( {{sessionId,userId}, std::move(data)} );
			}
		}
		catch( const Exception& e )
		{
			DBG( "Session Terminated on exception {}"sv, e.what() );
		}
	}
#ifdef HTTPS
	void Listen1( net::io_context& ioc, sp<ssl::context> pContext, tcp::endpoint endpoint, net::yield_context yld )noexcept
#else
	void Listen1( net::io_context& ioc, tcp::endpoint endpoint, net::yield_context yld )noexcept
#endif
	{
		Threading::SetThreadDscrptn("WebListener");
		tcp::acceptor acceptor( ioc );
		try
		{
			beast::error_code ec;
			acceptor.open( endpoint.protocol(), ec ); THROW_IF( ec, BeastException("open", move(ec)) );
			acceptor.set_option(net::socket_base::reuse_address(true), ec); THROW_IF( ec, BeastException( "set_option", move(ec)) );
			acceptor.bind( endpoint, ec ); THROW_IF( ec, BeastException( "bind", move(ec)) );
			acceptor.listen(net::socket_base::max_listen_connections, ec); THROW_IF( ec, BeastException( "listen", move(ec)) );
			for(;;)
			{
				tcp::socket socket(ioc);
				acceptor.async_accept( socket, yld[ec] );
				if(ec)
					BeastException::LogCode( "accept", ec  );
				else
				{
#ifdef HTTPS
					auto pStream = make_shared<SocketStream>( std::move(socket), ctx );
#else
					auto pStream = make_shared<SocketStream>( std::move(socket) );
#endif
					pStream->binary( true );
					boost::asio::spawn( acceptor.get_executor(), std::bind( &DoSession, pStream, std::placeholders::_1) );
						//boost::asio::spawn( acceptor.get_executor(), std::bind(&DoSession, make_shared<Stream>(move(socket), pContext)), std::placeholders::_1) );
				}
			}
		}
		catch( const Exception& e )
		{
			DBG( "Listener Terminated on exception {}"sv, e.what() );
		}
	}

	void WebCoSocket::Run()noexcept
	{
		net::io_context ioc{_threadCount};
		auto const address = net::ip::make_address( "0.0.0.0" );
#ifdef HTTPS
		ssl::context ctx{ssl::context::tlsv12};// The SSL context is required, and holds certificates
		load_server_certificate( ctx );// TODO: settings
		boost::asio::spawn( ioc, std::bind(&Listen1, std::ref(ioc), std::ref(ctx), tcp::endpoint{address, _port}, std::placeholders::_1) );// Spawn a listening port
#else
		boost::asio::spawn( ioc, std::bind(&Listen1, std::ref(ioc), tcp::endpoint{address, _port}, std::placeholders::_1) );// Spawn a listening port
#endif
		std::vector<std::thread> v; v.reserve( _threadCount - 1 );
		for( uint i = _threadCount - 1; i > 0; --i )
		{
			v.emplace_back( [&ioc]
			{
				ioc.run();//TODO see about run_for
			});
		}
		while( !Threading::GetThreadInterruptFlag().IsSet() )
			ioc.run(); //TODO see about interuptable threads
	}

	void WebCoSocket::SetLogin( const ClientKey& client, EAuthType type, sv email, bool emailVerified, sv name, sv pictureUrl, TimePoint expiration, sv key )noexcept
	{
		{
			shared_lock l{ _sessionMutex };
			var p = _sessions.find( client.SessionId ); RETURN_IF( p==_sessions.end(), "({})Could not find session."sv, client.SessionId );
			auto& settings = p->second;
			settings.AuthType = type;
			settings.Email = email;
			settings.EmailVerified = emailVerified;
			settings.Name = name;
			settings.PictureUrl = pictureUrl;
			settings.Expiration = expiration;
		}

		auto pValue = make_unique<Proto::Results::MessageValue>(); pValue->set_type( Proto::Results::EResults::Authentication ); pValue->set_int_value( client.ClientId );
		MessageType msg; msg.set_allocated_message( pValue.release() );
		AddOutgoing( move(msg), client.SessionId );
	}
}