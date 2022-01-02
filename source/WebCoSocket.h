#pragma once
#include <boost/beast/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include "WebRequestWorker.h"

namespace Jde::Markets::TwsWebSocket
{
	namespace beast = boost::beast;
	namespace net = boost::asio;
	namespace ssl = boost::asio::ssl;
	namespace websocket = beast::websocket;
	using tcp = boost::asio::ip::tcp;
	struct WebSendGateway;
	struct BeastException : public IException
	{
		BeastException( sv what, beast::error_code&& ec, ELogLevel level=ELogLevel::Trace, SRCE )noexcept;
		Ω IsTruncated( const beast::error_code& ec )noexcept{ return ec == net::ssl::error::stream_truncated; }
		Ω LogCode( const boost::system::error_code& ec, ELogLevel level, sv what )noexcept->void;

		using T=BeastException;
		α Clone()noexcept->sp<IException> override{ return ms<T>(move(*this)); }\
		α Move()noexcept->up<IException> override{ return mu<T>(move(*this)); }\
		α Ptr()->std::exception_ptr override{ return Jde::make_exception_ptr(move(*this)); }\
		[[noreturn]] α Throw()->void override{ throw move(*this); }
		beast::error_code ErrorCode;
	};

#ifdef HTTPS
	typedef websocket::stream<beast::ssl_stream<beast::tcp_stream>> SocketStream;
#else
	typedef websocket::stream<beast::tcp_stream> SocketStream;
#endif

	enum class EAuthType : uint8
	{
		None = 0,
		Google=1
	};
	constexpr array<sv,2> AuthTypeStrings = { "None", "Google" };

	struct SessionInfo
	{
		~SessionInfo();
		sp<SocketStream> StreamPtr;
		SessionPK SessionId;
		EAuthType AuthType{EAuthType::None};
		sp<std::atomic_flag> WriteLockPtr{ ms<std::atomic_flag>() };
		string Email;
		bool EmailVerified{false};
		string Name;
		string PictureUrl;
		TimePoint Expiration;
		UserPK UserId;
	};

	struct WebCoSocket final
	{
		Ω Create( sp<TwsClientSync> pClient, uint16 port, uint8 threadCount )noexcept->sp<WebCoSocket>;
		Ω Instance()noexcept->sp<WebCoSocket>{ return _pInstance; }

		Ω ForEachSession( function<void( const SessionInfo& x )> f )noexcept->void;
		Ω CoForEachSession( function<void( const SessionInfo& x )> f )noexcept->PoolAwait;
		α AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false)->void;
		Ω Send( MessageType&& msg, SessionPK id )noexcept->void;
		α AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept(false)->void;

		α AddConnection( sp<SocketStream> stream )noexcept->SessionPK;
		α RemoveConnection( SessionPK sessionId )noexcept->void;

		α HandleIncoming( WebRequestMessage&& data )noexcept{ _requestWorker.Push(move(data)); }
		α WebSend()noexcept->sp<WebSendGateway>{ return _pWebSend; }
		α SetLogin( const ClientKey& client, EAuthType type, sv email, bool emailVerified, sv name, sv pictureUrl, TimePoint expiration, sv key )noexcept->void;;
		Ω TryUserId( SessionPK sessionId )noexcept->UserPK;

		Ω SetLevel( ELogLevel l )noexcept->void;
	private:
		WebCoSocket( sp<TwsClientSync> pClient, uint16 port, uint8 threadCount )noexcept;

		α Run()noexcept->void;
		uint8 _threadCount;
		uint16 _port;
		sp<TwsClientSync> _pClient;
		sp<Threading::InterruptibleThread> _pThread;
		static sp<WebCoSocket> _pInstance;
		static flat_map<SessionPK,SessionInfo> _sessions; static shared_mutex _sessionMutex;
		static SessionPK _sessionId;
		sp<WebSendGateway> _pWebSend;
		WebRequestWorker _requestWorker;
	};
}