﻿#include "WebCoSocket.h"
#include "server_certificate.hpp"
#include "../../Framework/source/db/Database.h"
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
	ELogLevel _level{ Logging::TagLevel("web", [](auto l){ WrapperLog::SetLevel(l);}) };
	α WebCoSocket::SetLevel( ELogLevel l )noexcept->void{ _level=l; }
	α LogLevel()noexcept{ return _level; }

	BeastException::BeastException( sv what, beast::error_code&& ec, ELogLevel level, const source_location& sl )noexcept:
		IException{ {std::to_string(ec.value()), ec.message()}, format("{} returned ({{}}){{}}", what), sl, level },
		ErrorCode{ move(ec) }
	{}

	α BeastException::LogCode( const beast::error_code& ec, ELogLevel level, sv what )noexcept->void
	{
		if( BeastException::IsTruncated(ec) || ec.value()==125 || ec.value()==995 || what=="~DoSession" )
			LOG( level, "{} - ({}){}"sv, what, ec.value(), ec.message() );
		else
			WARN( "{} - ({}){}"sv, what, ec.value(), ec.message() );
	}
	//α BeastException::Log()const noexcept->void
	//{}
	sp<WebCoSocket> WebCoSocket::_pInstance;
	flat_map<SessionPK,SessionInfo> WebCoSocket::_sessions; shared_mutex WebCoSocket::_sessionMutex;
	SessionPK WebCoSocket::_sessionId{0};
	α WebCoSocket::Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept->sp<WebCoSocket> 
	{
		ASSERT( !_pInstance );
		return _pInstance = sp<WebCoSocket>( new WebCoSocket(settings, pClient) );
	}
	WebCoSocket::WebCoSocket( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept:
		_threadCount{ settings.TryGet<uint8>("threadCount").value_or(1) },
		_port{ settings.TryGet<uint16_t>("port").value_or((uint16)6812) },
		_pWebSend{ make_shared<WebSendGateway>(*this, pClient) },
		_requestWorker{ /**this,*/ _pWebSend, pClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebCoSocket", [&](){Run();} );
	}

	α WebCoSocket::AddConnection( sp<SocketStream> stream )noexcept->SessionPK 
	{
		var sessionId = ++_sessionId;
		unique_lock l{ _sessionMutex };
		_sessions.emplace( sessionId, SessionInfo{stream,make_shared<std::atomic_bool>(false)} );
		return sessionId;
	}
	α WebCoSocket::RemoveConnection( SessionPK sessionId )noexcept->void
	{
		unique_lock l{ _sessionMutex };
		_sessions.erase( sessionId );
	}


	α WebCoSocket::AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false)->void
	{
		AddOutgoing( vector<MessageType>{move(msg)}, id );
/**/
	}

	α WebCoSocket::AddOutgoing( MessageTypePtr pUnion, SessionPK /*id*/ )noexcept->void
	{
		ASSERT( false );
	}
	α WebCoSocket::AddOutgoing( const vector<MessageTypePtr>& /*messages*/, SessionPK /*id*/ )noexcept->void
	{
		ASSERT( false );
	}

	α WebCoSocket::AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept(false)->void
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
			var p = _sessions.find( id ); THROW_IFX( p==_sessions.end(), Exception(LogLevel(),format("({})Could not find session for outgoing transmission."sv, id)) );
			pStream = p->second.StreamPtr;
			pMutex = p->second.WriteLockPtr;
		}
		while( pMutex->exchange(true) )//TODO This function shouldn't lock.  async_write only wants one write at a time.  also, locked up program once
			std::this_thread::yield();
		pStream->async_write( boost::asio::buffer(pBuffer->data(), pBuffer->size()), [size, id, pMutex, pBuffer]( const boost::system::error_code& ec, size_t bytesTransferred )noexcept
		{
			*pMutex = false;
			if( ec )
			{
				BeastException::LogCode( ec, LogLevel(), format("({})async_write - killing session", id) );
				unique_lock l{ _sessionMutex };
				_sessions.erase( id );
			}
			else if( size!=bytesTransferred )
				DBG( "({})size({})!=bytesTransferred({})"sv, id, size, bytesTransferred );
		} );
	}

	α WebCoSocket::UserId( SessionPK sessionId )noexcept(false)->UserPK
	{
		var userPK = TryUserId( sessionId ); THROW_IFX( !userPK, Exception(LogLevel(), format("({})Could not find UserId.", sessionId)) );
		return userPK;
	}
	
	α WebCoSocket::TryUserId( SessionPK sessionId )noexcept->UserPK
	{
		shared_lock l{ _sessionMutex };
		var p = _sessions.find( sessionId ); 
		return p==_sessions.end() ? 0 : p->second.UserId;
	}

	string _host;
	α DoSession( sp<SocketStream> pStream, net::yield_context yld )->void
	{
		beast::error_code ec;
		SessionPK sessionId = 0;
		try
		{
#ifdef HTTPS
			pStream->next_layer().handshake( ssl::stream_base::server, ec );
#endif
			beast::get_lowest_layer(*pStream).expires_never();		// Turn off the timeout on the tcp_stream, because the websocket stream has its own timeout system.
			pStream->set_option( websocket::stream_base::timeout::suggested(beast::role_type::server) );// Set suggested timeout settings for the websocket
			pStream->set_option( websocket::stream_base::decorator( [](websocket::response_type& res)  // Set a decorator to change the Server of the handshake
			{
				res.set( http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-coro-ssl" );
			}) );
			pStream->async_accept( yld[ec] ); THROW_IFX( ec, BeastException("accept", move(ec)) );
			auto pWebCoSocket = WebCoSocket::Instance(); THROW_IF( !pWebCoSocket, "!pWebCoSocket" );
			sessionId = pWebCoSocket->AddConnection( pStream );
			UserPK userId{ 0 };
			//pWebCoSocket->SetLogin( {{sessionId,userId },0}, EAuthType::Google, "foo@gmail.com", true, "Mr foo", "", Clock::now()+24h, {} );
			for( ;; )
			{
				beast::flat_buffer buffer;
				pStream->async_read( buffer, yld[ec] );
				if( ec )
					break;
				auto data = boost::beast::buffers_to_string( buffer.data() );
				userId = userId ? userId : pWebCoSocket->TryUserId( sessionId );
				pWebCoSocket->HandleIncoming( {{sessionId,userId}, std::move(data)} );
			}
			BeastException::LogCode( ec, LogLevel(), "~DoSession" );
		}
		catch( const IException& e )
		{
			DBG( "({})Session Terminated on exception {}"sv, sessionId, e.what() );
			if( auto pWebCoSocket = WebCoSocket::Instance(); pWebCoSocket && sessionId )
				pWebCoSocket->RemoveConnection( sessionId );
		}
	}
#ifdef HTTPS
	α Listen1( net::io_context& ioc, sp<ssl::context> pContext, tcp::endpoint endpoint, net::yield_context yld )noexcept->void
#else
	α Listen1( net::io_context& ioc, tcp::endpoint endpoint, net::yield_context yld )noexcept->void
#endif
	{
		Threading::SetThreadDscrptn("WebListener");
		tcp::acceptor acceptor( ioc );
		try
		{
			beast::error_code ec;
			acceptor.open( endpoint.protocol(), ec ); THROW_IFX( ec, BeastException("open", move(ec), ELogLevel::Critical) );
			acceptor.set_option(net::socket_base::reuse_address(true), ec); THROW_IFX( ec, BeastException("set_option", move(ec), ELogLevel::Critical) );
			acceptor.bind( endpoint, ec ); THROW_IFX( ec, BeastException("bind", move(ec), ELogLevel::Critical) );
			acceptor.listen( net::socket_base::max_listen_connections, ec); THROW_IFX( ec, BeastException("listen", move(ec), ELogLevel::Critical) );
			for(;;)
			{
				tcp::socket socket(ioc);
				acceptor.async_accept( socket, yld[ec] );
				if(ec)
					BeastException::LogCode( ec, LogLevel(), "accept" );
				else
				{
#ifdef HTTPS
					auto pStream = make_shared<SocketStream>( std::move(socket), ctx );
#else
					auto pStream = make_shared<SocketStream>( std::move(socket) );
#endif
					pStream->binary( true );
					boost::asio::spawn( acceptor.get_executor(), std::bind( &DoSession, pStream, std::placeholders::_1) );
				}
			}
		}
		catch( const IException& )
		{}
	}

	α WebCoSocket::Run()noexcept->void
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

	α WebCoSocket::SetLogin( const ClientKey& client, EAuthType type, sv email, bool emailVerified, sv name, sv pictureUrl, TimePoint expiration, sv /*key*/ )noexcept->void
	{
		UserPK userId;
		{
			shared_lock l{ _sessionMutex };
			var p = _sessions.find( client.SessionId ); RETURN_IF( p==_sessions.end(), "({})Could not find session for Login.", client.SessionId );
			auto& settings = p->second;
			settings.AuthType = type;
			settings.Email = email;
			settings.EmailVerified = emailVerified;
			settings.Name = name;
			settings.PictureUrl = pictureUrl;
			settings.Expiration = expiration;
			userId = settings.UserId = DB::TryScaler<UserPK>( "select id from um_users where name=? and authenticator_id=?", {email,(uint)type} ).value_or( 0 );
		}

		auto pValue = make_unique<Proto::Results::MessageValue>(); pValue->set_type( Proto::Results::EResults::Authentication ); 
		if( userId )
			pValue->set_int_value( client.ClientId );
		else
		{
			DBG( "({})Could not find user name='{}' for authenticator='{}'", client.SessionId, email, Str::FromEnum(AuthTypeStrings, type) );
			pValue->set_string_value( "Could not find user name" );
		}
		MessageType msg; msg.set_allocated_message( pValue.release() );
		AddOutgoing( move(msg), client.SessionId );
	}
}