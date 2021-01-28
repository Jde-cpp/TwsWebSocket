#pragma once
#include <boost/beast/ssl.hpp>
/*
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/beast/version.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>

*/
//https://www.boost.org/doc/libs/1_75_0/libs/beast/example/http/server/stackless-ssl/http_server_stackless_ssl.cpp

//namespace boost::asio::ip{ class tcp; }
namespace Jde::Markets
{
	struct TwsClientSync;

namespace TwsWebSocket
{
	namespace beast = boost::beast;         // from <boost/beast.hpp>
	namespace http = beast::http;           // from <boost/beast/http.hpp>
	namespace net = boost::asio;            // from <boost/asio.hpp>
	namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

	struct BeastException : public Exception
	{
		BeastException( sv what, beast::error_code&& ec )noexcept;
		static bool IsTruncated( const beast::error_code& ec )noexcept{ return ec == net::ssl::error::stream_truncated; }
		static void Log( sv what, const beast::error_code& )noexcept;

		beast::error_code ErrorCode;
	};

	//namespace Messages{ struct Message; struct Application; struct Strings; }
	struct WebCoRequest final : public IShutdown
	{
		static sp<WebCoRequest> Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept;
		static sp<WebCoRequest> Instance()noexcept{ return _pInstance; }
		void Shutdown()noexcept override;
	private:
		WebCoRequest( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept;
		sp<TwsClientSync> _pClient;
		uint16 _port;
		static sp<WebCoRequest> _pInstance;
	};


	struct Session: public boost::asio::coroutine, public std::enable_shared_from_this<Session>
	{
    explicit Session( tcp::socket&& socket, ssl::context& ctx, const std::shared_ptr<std::string const>& doc_root ):
	 	_stream(std::move(socket), ctx),
		 _doc_root(doc_root),
		 _lambda(*this)
    {}
    void run() // We need to be executing within a strand to perform async operations on the I/O objects in this session. Although not strictly necessary for single-threaded contexts, this example code is written to be thread-safe by default.
    {
        net::dispatch( _stream.get_executor(), beast::bind_front_handler( &Session::Loop, shared_from_this(), beast::error_code{}, 0, false) );
    }
    void Loop( beast::error_code ec, std::size_t bytes_transferred, bool close );
	private:
		struct SendLambda //Used to send an HTTP message.
		{
			explicit SendLambda( Session& self ) : _self(self){}
			template<bool isRequest, class Body, class Fields>
			void operator()( http::message<isRequest, Body, Fields>&& msg )const
			{
				auto p = _self._pKeepAlive = std::make_shared<http::message<isRequest, Body, Fields>>( std::move(msg) );// The lifetime of the message has to extend for the duration of the async operation so we use a shared_ptr to manage it.
				http::async_write( _self._stream, *p, std::bind(&Session::Loop, _self.shared_from_this(), std::placeholders::_1, std::placeholders::_2, p->need_eof()) );
			}
		private:
			Session& _self;
		};

		beast::ssl_stream<beast::tcp_stream> _stream;
		beast::flat_buffer _buffer;
		std::shared_ptr<std::string const> _doc_root;
		http::request<http::string_body> _req;
		std::shared_ptr<void> _pKeepAlive;
		SendLambda _lambda;
};


	struct Listener : public boost::asio::coroutine, public std::enable_shared_from_this<Listener>
	{
    	Listener( net::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint )noexcept(false);
		void Run()noexcept(false){ Loop(); }
	private:
		void Loop( beast::error_code ec = {} )noexcept(false);

		std::shared_ptr<const string> _docRoot;
		net::io_context& _ioc;
		ssl::context& _ctx;
		tcp::acceptor _acceptor;
		tcp::socket _socket;
	};
}}