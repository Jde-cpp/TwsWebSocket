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
	struct BeastException : public Exception
	{
		BeastException( sv what, beast::error_code&& ec, ELogLevel level=ELogLevel::Trace )noexcept;
		Ω IsTruncated( const beast::error_code& ec )noexcept{ return ec == net::ssl::error::stream_truncated; }
		Ω LogCode( const boost::system::error_code& ec, ELogLevel level, sv what )noexcept->void;

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

	struct SessionInfo
	{
		sp<SocketStream> StreamPtr;
		sp<atomic<bool>> WriteLockPtr;
		EAuthType AuthType{EAuthType::None};
		string Email;
		bool EmailVerified{false};
		string Name;
		string PictureUrl;
		TimePoint Expiration;
		SessionPK SessionId;
		UserPK UserId;
	};

	struct WebCoSocket final
	{
		Ω Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept->sp<WebCoSocket>;
		Ω Instance()noexcept->sp<WebCoSocket>{ return _pInstance; }

		void AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false);
		void AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept;
		void AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept;
		void AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept(false);

		SessionPK AddConnection( sp<SocketStream> stream )noexcept;
		void RemoveConnection( SessionPK sessionId )noexcept;

		void HandleIncoming( WebRequestMessage&& data )noexcept{ _requestWorker.Push(move(data)); }
		sp<WebSendGateway> WebSend(){ return _pWebSend; }
		void SetLogin( const ClientKey& client, EAuthType type, sv email, bool emailVerified, sv name, sv pictureUrl, TimePoint expiration, sv key )noexcept;
		α UserId( SessionPK sessionId )noexcept(false)->UserPK;
		α TryUserId( SessionPK sessionId )noexcept->UserPK;

		Ω SetLevel( ELogLevel l )noexcept->void;
	private:
		WebCoSocket( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept;

		void Run()noexcept;
		uint8 _threadCount;
		uint16_t _port;
		sp<TwsClientSync> _pClient;
		sp<Threading::InterruptibleThread> _pThread;
		static sp<WebCoSocket> _pInstance;
		static flat_map<SessionPK,SessionInfo> _sessions; static shared_mutex _sessionMutex;
		static SessionPK _sessionId;
		sp<WebSendGateway> _pWebSend;
		WebRequestWorker _requestWorker;
	};
}