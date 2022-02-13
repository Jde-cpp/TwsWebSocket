#include "WebCoSocket.h"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <jde/markets/types/proto/ResultsMessage.h>
#include "server_certificate.hpp"
#include "../../Framework/source/db/Database.h"
#include "../../MarketLibrary/source/OrderManager.h"


//https://www.boost.org/doc/libs/develop/libs/beast/example/websocket/server/coro-ssl/websocket_server_coro_ssl.cpp
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	static const LogTag& _logLevel = Logging::TagLevel( "app.web" );

	BeastException::BeastException( sv what, beast::error_code&& ec, ELogLevel level, const source_location& sl )noexcept:
		IException{ {std::to_string(ec.value()), ec.message()}, format("{} returned ({{}}){{}}", what), sl, (uint)ec.value(), level },
		ErrorCode{ move(ec) }
	{}

	α BeastException::LogCode( const beast::error_code& ec, ELogLevel level, sv what )noexcept->void
	{
		if( BeastException::IsTruncated(ec) || ec.value()==125 || ec.value()==995 || what=="~DoSession" )
			LOGL( level, "{} - ({}){}"sv, what, ec.value(), ec.message() );
		else
			WARN( "{} - ({}){}"sv, what, ec.value(), ec.message() );
	}

	SessionInfo::~SessionInfo()
	{
		LOG( "({})~SessionInfo()", SessionId );
	}
	sp<WebCoSocket> WebCoSocket::_pInstance;
	flat_map<SessionPK,SessionInfo> WebCoSocket::_sessions; shared_mutex WebCoSocket::_sessionMutex;
	SessionPK WebCoSocket::_sessionId{0};
	α WebCoSocket::Create( sp<TwsClientSync> pClient, uint16 port, uint8 threadCount )noexcept->sp<WebCoSocket>
	{
		ASSERT( !_pInstance );
		return _pInstance = sp<WebCoSocket>( new WebCoSocket{pClient, port, threadCount} );
	}
	WebCoSocket::WebCoSocket( sp<TwsClientSync> pClient, uint16 port, uint8 threadCount )noexcept:
		_threadCount{ threadCount },
		_port{ port },
		_pWebSend{ make_shared<WebSendGateway>(*this, pClient) },
		_requestWorker{ /**this,*/ _pWebSend, pClient }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebCoSocket", [&](){Run();} );
		OrderManager::Listen( _pWebSend );
	}

	α WebCoSocket::AddConnection( sp<SocketStream> stream )noexcept->SessionPK
	{
		var sessionId = ++_sessionId;
		unique_lock l{ _sessionMutex };
		_sessions.emplace( sessionId, SessionInfo{stream,sessionId} );
		return sessionId;
	}
	α WebCoSocket::ForEachSession( function<void( const SessionInfo& x )> f )noexcept->void
	{
		shared_lock l{ _sessionMutex };
		var sessions = Collections::Values( _sessions );
		l.unlock();
		for( var& s : sessions )
			f( s );
	}
	α WebCoSocket::CoForEachSession( function<void( const SessionInfo& x )> f )noexcept->PoolAwait
	{
		return PoolAwait( [f]()
		{
			shared_lock l{ _sessionMutex };
			var sessions = Collections::Values( _sessions );
			l.unlock();
			for( var& s : sessions )
				f( s );
		});
	}
	α WebCoSocket::RemoveConnection( SessionPK sessionId )noexcept->void
	{
		unique_lock l{ _sessionMutex };
		_sessions.erase( sessionId );
	}


	α WebCoSocket::AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false)->void
	{
		AddOutgoing( vector<MessageType>{move(msg)}, id );

	}
	α WebCoSocket::Send( MessageType&& msg, SessionPK id )noexcept->void
	{
		try
		{
			_pInstance->AddOutgoing( move(msg), id );
		}
		catch( IException& )
		{}
	}

	α WebCoSocket::AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept->void
	{
		Proto::Results::Transmission transmission;
		for( auto&& msg : messages )
			*transmission.add_messages() = move( msg );
		const size_t size = transmission.ByteSizeLong();
		auto pBuffer = make_shared<std::vector<char>>( size );
		transmission.SerializeToArray( pBuffer->data(), (int)pBuffer->size() );

		sp<SocketStream> pStream; sp<std::atomic_flag> pMutex;
		{
			shared_lock l{ _sessionMutex };
			var p = _sessions.find( id ); RETURN_IF( p==_sessions.end(), "({})Could not find session for outgoing transmission.", id );
			pStream = p->second.StreamPtr;
			pMutex = p->second.WriteLockPtr;
		}
		if( !pMutex )//not sure why this happens.
			return;
		while( pMutex->test_and_set(std::memory_order_acquire) )
		{
         while( pMutex->test(std::memory_order_relaxed) )
				std::this_thread::yield();
		}
		pStream->async_write( boost::asio::buffer(pBuffer->data(), pBuffer->size()), [size, id, pMutex2=pMutex, pBuffer]( const boost::system::error_code& ec, size_t bytesTransferred )noexcept
		{
			pMutex2->clear( std::memory_order_release );
			if( ec )
			{
				BeastException::LogCode( ec, _logLevel.Level, format("({})async_write - killing session", id) );
				unique_lock l2{ _sessionMutex };
				_sessions.erase( id );
			}
			else if( size!=bytesTransferred )
				DBG( "({})size({})!=bytesTransferred({})"sv, id, size, bytesTransferred );
		} );
	}

	α WebCoSocket::TryUserId( SessionPK s )noexcept->UserPK
	{
		UserPK pk{};
		if( auto pInstance = _pInstance; pInstance )
		{
			sl l{ pInstance->_sessionMutex };
			if( var p = pInstance->_sessions.find(s); p!=pInstance->_sessions.end() )
			{
				if( pk = p->second.UserId; !pk )
				{
					if( auto pEmail = Settings::Env("um/user.email"); pEmail )
					{
						l.unlock();
						SetLogin( {{s, 0}, 0}, EAuthType::Google, *pEmail, true, *pEmail, "", Clock::now()+std::chrono::hours(100 * 24), "key" );
						l.lock();
						if( var p2 = pInstance->_sessions.find(s); p2!=pInstance->_sessions.end() )
							pk = p2->second.UserId;
					}
				}
			}
		}
		return pk;
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
			BeastException::LogCode( ec, _logLevel.Level, "~DoSession" );
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
					BeastException::LogCode( ec, _logLevel.Level, "accept" );
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
		boost::asio::spawn( ioc, std::bind(&Listen1, std::ref(ioc), tcp::endpoint{address, (uint16_t)_port}, std::placeholders::_1) );// Spawn a listening port
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

		if( auto p = _pInstance; p )
		{
			if( userId )
				p->AddOutgoing( ToMessage(Proto::Results::EResults::Authentication, client.ClientId), client.SessionId );
			else
			{
				DBG( "({})Could not find user name='{}' for authenticator='{}'", client.SessionId, email, Str::FromEnum(AuthTypeStrings, type) );
				p->AddOutgoing( ToMessage(Proto::Results::EResults::Authentication, std::to_string(client.ClientId)), client.SessionId );
			}
		}
	}
}