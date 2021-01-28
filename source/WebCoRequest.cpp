//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: HTTP SSL server, stackless coroutine
//
//------------------------------------------------------------------------------
#include "WebCoSocket.h"
#include "TypeDefs.h"
//#include "server_certificate.hpp"

/*#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
*/
#include <boost/asio/yield.hpp>
#define var const auto
namespace Jde::Markets::TwsWebSocket
{

	BeastException::BeastException( sv what, beast::error_code&& ec )noexcept:
		Exception{ "{} returned {}", what, ec.message() },
		ErrorCode{ move(ec) }
	{}

	void BeastException::Log( sv what, const beast::error_code& ec )noexcept
	{
		if( BeastException::IsTruncated(ec) )
			DBG( "async_accept Truncated - {}"sv, ec.message() );
		else
			WARN( "async_accept Failed - {}"sv, ec.message() );
	}


	sp<WebCoSocket> WebCoSocket::Create( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept
	{
		ASSERT( !_pInstance );
		return _pInstance = sp<WebCoSocket>( new WebCoSocket{settings, pClient} );
	}

	WebCoSocket::WebCoSocket( const Settings::Container& settings, sp<TwsClientSync> pClient )noexcept:
	//	_context{ settings.Get2<uint8>("threadCount").value_or(1) },
		_pClient{ pClient },
		_port{ settings.Get2<uint16>("port").value_or(6811) }
	{}

	void WebCoSocket::Shutdown()noexcept
	{
		DBG0( "WebCoSocket::Shutdown"sv );
		_pClient = nullptr;
		_pInstance = nullptr;
		DBG0( "WebCoSocket::Shutdown - Leaving"sv );
	}

   Listener::Listener( net::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint ):
		_ioc(ioc),
		_ctx(ctx),
		_acceptor(net::make_strand(ioc)),
		_socket(net::make_strand(ioc))
	{
		beast::error_code ec;
		_acceptor.open( endpoint.protocol(), ec );
		THROW_IF( ec, BeastException("_acceptor.open", move(ec)) );
		_acceptor.set_option( net::socket_base::reuse_address(true), ec );
		THROW_IF( ec, BeastException("reuse_address", move(ec)) );
		_acceptor.bind( endpoint, ec );
		THROW_IF( ec, BeastException("_acceptor.bid", move(ec)) );
		_acceptor.listen( net::socket_base::max_listen_connections, ec );
		THROW_IF( ec, BeastException("_acceptor.listen", move(ec)) );
	}

	void Listener::Loop( beast::error_code ec )
	{
		reenter(*this)
		{
			for(;;)
			{
				yield _acceptor.async_accept( _socket, std::bind( &Listener::Loop, shared_from_this(), std::placeholders::_1) );
				if( ec )
					BeastException::Log( "async_accept", ec );
				else
					std::make_shared<Session>( std::move(_socket), _ctx, _docRoot )->run();

				_socket = tcp::socket(net::make_strand(_ioc));
			}
		}
	}

	template<class Body, class Allocator, class Send>
	void HandleRequest( beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send )
	{
		var bad_request = [&req]( beast::string_view why )// Returns a bad request response
		{
			http::response<http::string_body> res{ http::status::bad_request, req.version() };
			res.set( http::field::server, BOOST_BEAST_VERSION_STRING );
			res.set( http::field::content_type, "text/html" );
			res.keep_alive( req.keep_alive() );
			res.body() = std::string( why );
			res.prepare_payload();
			return res;
		};

		var not_found = [&req](beast::string_view target)// Returns a not found response
		{
			http::response<http::string_body> res{http::status::not_found, req.version()};
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.keep_alive(req.keep_alive());
			res.body() = "The resource '" + std::string(target) + "' was not found.";
			res.prepare_payload();
			return res;
		};


		var server_error = [&req](beast::string_view what)// Returns a server error response
		{
			http::response<http::string_body> res{http::status::internal_server_error, req.version()};
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, "text/html");
			res.keep_alive(req.keep_alive());
			res.body() = "An error occurred: '" + std::string(what) + "'";
			res.prepare_payload();
			return res;
		};

		// Make sure we can handle the method
		if( req.method() != http::verb::get && req.method() != http::verb::head )
			return send(bad_request("Unknown HTTP-method"));

		// Request path must be absolute and not contain "..".
		if( req.target().empty() ||
			req.target()[0] != '/' ||
			req.target().find("..") != beast::string_view::npos)
			return send(bad_request("Illegal request-target"));

		// Build the path to the requested file
		std::string path = path_cat(doc_root, req.target());
		if(req.target().back() == '/')
			path.append("index.html");

		// Attempt to open the file
		beast::error_code ec;
		http::file_body::value_type body;
		body.open(path.c_str(), beast::file_mode::scan, ec);

		// Handle the case where the file doesn't exist
		if(ec == beast::errc::no_such_file_or_directory)
			return send(not_found(req.target()));

		// Handle an unknown error
		if(ec)
			return send(server_error(ec.message()));

		// Cache the size since we need it after the move
		var size = body.size();

		// Respond to HEAD request
		if(req.method() == http::verb::head)
		{
			http::response<http::empty_body> res{http::status::ok, req.version()};
			res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(http::field::content_type, mime_type(path));
			res.content_length(size);
			res.keep_alive(req.keep_alive());
			return send(std::move(res));
		}

		// Respond to GET request
		http::response<http::file_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(http::status::ok, req.version())};
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, mime_type(path));
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return send(std::move(res));
	}

	void Session::Loop( beast::error_code ec, std::size_t bytes_transferred, bool close )
	{
		boost::ignore_unused(bytes_transferred);
		reenter(*this)
		{
			beast::get_lowest_layer( _stream ).expires_after( 30s );
			yield _stream.async_handshake( ssl::stream_base::server, std::bind(&Session::Loop, shared_from_this(), std::placeholders::_1, 0, false) );
			if( ec )
				return BeastException::Log( "async_handshake", ec );
			for(;;)
			{
				beast::get_lowest_layer(_stream).expires_after( 30s );// Set the timeout.
            _req = {};    // Make the request empty before reading, otherwise the operation behavior is undefined.
				yield http::async_read( _stream, _buffer, _req, std::bind( &Session::Loop, shared_from_this(), std::placeholders::_1, std::placeholders::_2, false) );
				if( ec == http::error::end_of_stream )// The remote host closed the connection
					break;
            if( ec )
					return BeastException::Log( "read", ec );

				yield HandleRequest( *_doc_root, std::move(_req), _lambda );
				if(ec)
					return BeastException::Log( "write", ec );
				if( close )// This means we should close the connection, usually because the response indicated the "Connection: close" semantic.
					break;

				res_ = nullptr;                // We're done with the response so delete it
			}
			beast::get_lowest_layer(stream_).expires_after( 30s );// Set the timeout.

			yield stream_.async_shutdown( std::bind( &Session::Loop, shared_from_this(), std::placeholders::_1, 0, false) );
			if(ec)
				return BeastException::Log( "shutdown", ec );
		}
	}
    #include <boost/asio/unyield.hpp>
}
// Return a reasonable mime type based on the extension of a file.
beast::string_view
mime_type(beast::string_view path)
{
    using beast::iequals;
    var ext = [&path]
    {
        var pos = path.rfind(".");
        if(pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }();
    if(iequals(ext, ".htm"))  return "text/html";
    if(iequals(ext, ".html")) return "text/html";
    if(iequals(ext, ".php"))  return "text/html";
    if(iequals(ext, ".css"))  return "text/css";
    if(iequals(ext, ".txt"))  return "text/plain";
    if(iequals(ext, ".js"))   return "application/javascript";
    if(iequals(ext, ".json")) return "application/json";
    if(iequals(ext, ".xml"))  return "application/xml";
    if(iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if(iequals(ext, ".flv"))  return "video/x-flv";
    if(iequals(ext, ".png"))  return "image/png";
    if(iequals(ext, ".jpe"))  return "image/jpeg";
    if(iequals(ext, ".jpeg")) return "image/jpeg";
    if(iequals(ext, ".jpg"))  return "image/jpeg";
    if(iequals(ext, ".gif"))  return "image/gif";
    if(iequals(ext, ".bmp"))  return "image/bmp";
    if(iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if(iequals(ext, ".tiff")) return "image/tiff";
    if(iequals(ext, ".tif"))  return "image/tiff";
    if(iequals(ext, ".svg"))  return "image/svg+xml";
    if(iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
    beast::string_view base,
    beast::string_view path)
{
    if(base.empty())
        return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for(auto& c : result)
        if(c == '/')
            c = path_separator;
#else
    char constexpr path_separator = '/';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
    class Body, class Allocator,
    class Send>
void
handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req,
    Send&& send)
{
    // Returns a bad request response
    var bad_request =
    [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    var not_found =
    [&req](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    var server_error =
    [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if( req.method() != http::verb::get &&
        req.method() != http::verb::head)
        return send(bad_request("Unknown HTTP-method"));

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return send(bad_request("Illegal request-target"));

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    if(req.target().back() == '/')
        path.append("index.html");

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if(ec == beast::errc::no_such_file_or_directory)
        return send(not_found(req.target()));

    // Handle an unknown error
    if(ec)
        return send(server_error(ec.message()));

    // Cache the size since we need it after the move
    var size = body.size();

    // Respond to HEAD request
    if(req.method() == http::verb::head)
    {
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(beast::error_code ec, char const* what)
{
    // ssl::error::stream_truncated, also known as an SSL "short read",
    // indicates the peer closed the connection without performing the
    // required closing handshake (for example, Google does this to
    // improve performance). Generally this can be a security issue,
    // but if your communication protocol is self-terminated (as
    // it is with both HTTP and WebSocket) then you may simply
    // ignore the lack of close_notify.
    //
    // https://github.com/boostorg/beast/issues/38
    //
    // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
    //
    // When a short read would cut off the end of an HTTP message,
    // Beast returns the error beast::http::error::partial_message.
    // Therefore, if we see a short read here, it has occurred
    // after the message has been completed, so it is safe to ignore it.

    if(ec == net::ssl::error::stream_truncated)
        return;

    std::cerr << what << ": " << ec.message() << "\n";
}

class session
    : public boost::asio::coroutine
    , public std::enable_shared_from_this<session>
{
    // This is the C++11 equivalent of a generic lambda.
    // The function object is used to send an HTTP message.
    struct send_lambda
    {
        session& self_;

        explicit
        send_lambda(session& self)
            : self_(self)
        {
        }

        template<bool isRequest, class Body, class Fields>
        void
        operator()(http::message<isRequest, Body, Fields>&& msg) const
        {
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            auto sp = std::make_shared<
                http::message<isRequest, Body, Fields>>(std::move(msg));

            // Store a type-erased version of the shared
            // pointer in the class to keep it alive.
            self_.res_ = sp;

            // Write the response
            http::async_write(
                self_.stream_,
                *sp,
                std::bind(
                    &session::loop,
                    self_.shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2,
                    sp->need_eof()));
        }
    };

    beast::ssl_stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<std::string const> doc_root_;
    http::request<http::string_body> req_;
    std::shared_ptr<void> res_;
    send_lambda lambda_;

public:
    // Take ownership of the socket
    explicit
    session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<std::string const> const& doc_root)
        : stream_(std::move(socket), ctx)
        , doc_root_(doc_root)
        , lambda_(*this)
    {
    }

    // Start the asynchronous operation
    void
    run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session.Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(stream_.get_executor(),
                      beast::bind_front_handler(&session::loop,
                                                shared_from_this(),
                                                beast::error_code{},
                                                0,
                                                false));
    }

    #include <boost/asio/yield.hpp>

    void
    loop(
        beast::error_code ec,
        std::size_t bytes_transferred,
        bool close)
    {
        boost::ignore_unused(bytes_transferred);
        reenter(*this)
        {
            // Set the timeout.
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

            // Perform the SSL handshake
            yield stream_.async_handshake(
                ssl::stream_base::server,
                std::bind(
                    &session::loop,
                    shared_from_this(),
                    std::placeholders::_1,
                    0,
                    false));
            if(ec)
                return fail(ec, "handshake");

            for(;;)
            {
                // Set the timeout.
                beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

                // Make the request empty before reading,
                // otherwise the operation behavior is undefined.
                req_ = {};

                // Read a request
                yield http::async_read(stream_, buffer_, req_,
                    std::bind(
                        &session::loop,
                        shared_from_this(),
                        std::placeholders::_1,
                        std::placeholders::_2,
                        false));
                if(ec == http::error::end_of_stream)
                {
                    // The remote host closed the connection
                    break;
                }
                if(ec)
                    return fail(ec, "read");

                // Send the response
                yield handle_request(*doc_root_, std::move(req_), lambda_);
                if(ec)
                    return fail(ec, "write");
                if(close)
                {
                    // This means we should close the connection, usually because
                    // the response indicated the "Connection: close" semantic.
                    break;
                }

                // We're done with the response so delete it
                res_ = nullptr;
            }

            // Set the timeout.
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

            // Perform the SSL shutdown
            yield stream_.async_shutdown(
                std::bind(
                    &session::loop,
                    shared_from_this(),
                    std::placeholders::_1,
                    0,
                    false));
            if(ec)
                return fail(ec, "shutdown");

            // At this point the connection is closed gracefully
        }
    }

    #include <boost/asio/unyield.hpp>
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener
    : public boost::asio::coroutine
    , public std::enable_shared_from_this<listener>
{

public:
    listener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint,
        std::shared_ptr<std::string const> const& doc_root)
        : ioc_(ioc)
        , ctx_(ctx)
        , acceptor_(net::make_strand(ioc))
        , socket_(net::make_strand(ioc))
        , doc_root_(doc_root)
    {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if(ec)
        {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec)
        {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if(ec)
        {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if(ec)
        {
            fail(ec, "listen");
            return;
        }
    }

    // Start accepting incoming connections
    void
    run()
    {
        loop();
    }

private:

    #include <boost/asio/yield.hpp>

    void
    loop(beast::error_code ec = {})
    {
        reenter(*this)
        {
            for(;;)
            {
                yield acceptor_.async_accept(
                    socket_,
                    std::bind(
                        &listener::loop,
                        shared_from_this(),
                        std::placeholders::_1));
                if(ec)
                {
                    fail(ec, "accept");
                }
                else
                {
                    // Create the session and run it
                    std::make_shared<session>(
                        std::move(socket_),
                        ctx_,
                        doc_root_)->run();
                }

                // Make sure each session gets its own strand
                socket_ = tcp::socket(net::make_strand(ioc_));
            }
        }
    }

    #include <boost/asio/unyield.hpp>
};

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // Check command line arguments.
    if (argc != 5)
    {
        std::cerr <<
            "Usage: http-server-stackless-ssl <address> <port> <doc_root> <threads>\n" <<
            "Example:\n" <<
            "    http-server-stackless-ssl 0.0.0.0 8080 . 1\n";
        return EXIT_FAILURE;
    }
    var address = net::ip::make_address(argv[1]);
    var port = static_cast<unsigned short>(std::atoi(argv[2]));
    var doc_root = std::make_shared<std::string>(argv[3]);
    var threads = std::max<int>(1, std::atoi(argv[4]));

    // The io_context is required for all I/O
    net::io_context ioc{threads};

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    // Create and launch a listening port
    std::make_shared<listener>(
        ioc,
        ctx,
        tcp::endpoint{address, port},
        doc_root)->run();

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ioc]
        {
            ioc.run();
        });
    ioc.run();

    return EXIT_SUCCESS;
}