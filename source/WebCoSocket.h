#pragma once
#include <boost/beast/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include "WebRequestWorker.h"

namespace Jde::Markets::TwsWebSocket
{
	namespace beast = boost::beast;         // from <boost/beast.hpp>
	namespace net = boost::asio;            // from <boost/asio.hpp>
	namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
	namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
	struct WebSendGateway;
	struct BeastException : public Exception
	{
		BeastException( sv what, beast::error_code&& ec )noexcept;
		static bool IsTruncated( const beast::error_code& ec )noexcept{ return ec == net::ssl::error::stream_truncated; }
		static void LogCode( sv what, const beast::error_code& )noexcept;

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
		static sp<WebCoSocket> Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept;
		static sp<WebCoSocket> Instance()noexcept{ return _pInstance; }

		void AddOutgoing( MessageType&& msg, SessionPK id )noexcept(false);
		void AddOutgoing( MessageTypePtr pUnion, SessionPK id )noexcept;
		void AddOutgoing( const vector<MessageTypePtr>& messages, SessionPK id )noexcept;
		//void AddOutgoing( vector<Proto::Results::MessageUnion>&& messages, SessionPK id )noexcept;
		void AddOutgoing( const vector<Proto::Results::MessageUnion>& messages, SessionPK id )noexcept(false);

		SessionPK AddConnection( sp<SocketStream> stream )noexcept;
		void RemoveConnection( SessionPK sessionId )noexcept;

		void HandleIncoming( WebRequestMessage&& data )noexcept{ _requestWorker.Push(move(data)); }
		sp<WebSendGateway> WebSend(){ return _pWebSend; }
		void SetLogin( const ClientKey& client, EAuthType type, sv email, bool emailVerified, sv name, sv pictureUrl, TimePoint expiration, sv key )noexcept;
		UserPK UserId( SessionPK sessionId )noexcept(false);
		UserPK TryUserId( SessionPK sessionId )noexcept{ return Try<UserPK>( [sessionId,this]{ return UserId(sessionId); }).value_or(0); };
	private:
		WebCoSocket( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept;
		//void Listen( net::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint, net::yield_context yield );

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