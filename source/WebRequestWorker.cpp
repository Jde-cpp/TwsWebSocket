#include "WebRequestWorker.h"
#include <jde/markets/types/proto/ResultsMessage.h>
#include "./BlocklyWorker.h"
#include "./Flex.h"
#include "requests/News.h"
#include "requests/Twitter.h"
#include "requests/Reddit.h"
#include "./PreviousDayValues.h"
#include "./WatchListData.h"
#include "./WebCoSocket.h"
#include "./requests/EdgarRequests.h"
#include "../../Framework/source/db/GraphQL.h"
#include "../../MarketLibrary/source/data/HistoricalDataCache.h"
#include "../../MarketLibrary/source/data/OptionData.h"
#include "../../MarketLibrary/source/data/StatAwait.h"
#include "../../Google/source/TokenInfo.h"
#include "../../Ssl/source/Ssl.h"

#define _sync TwsClientSync::Instance()
#define var const auto
#define USE(p)	auto t = p; if( t ) t->

namespace Jde::Markets::TwsWebSocket
{
	static const LogTag& _logLevel = Logging::TagLevel( "app-webRequests" );
	WebRequestWorker::WebRequestWorker( /*WebSocket& webSocketParent,*/ sp<WebSendGateway> webSend, sp<TwsClientSync> pTwsClient )noexcept:
		_pTwsSend{ make_shared<TwsSendWorker>(webSend, pTwsClient) },
		_pWebSend{webSend},
		_pBlocklyWorker{ make_shared<BlocklyWorker>(_pWebSend) }
	{
		_pThread = make_shared<Threading::InterruptibleThread>( "WebRequestWorker", [&](){Run();} );
	}
	α WebRequestWorker::Push( QueueType&& msg )noexcept->void
	{
		_queue.Push( std::move(msg) );
	}
	α WebRequestWorker::Run()noexcept->void
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() || !_queue.empty() )
		{
			if( auto v =_queue.TryPop(5s); v )
				HandleRequest( std::move(*v) );
		}
	}
#define ARG(x) {{{s}, x}, _pWebSend}
	α WebRequestWorker::HandleRequest( QueueType&& msg )noexcept->void
	{
		try
		{
			Proto::Requests::RequestTransmission transmission;
			var data = std::move( msg.Data );
			if( google::protobuf::io::CodedInputStream stream{reinterpret_cast<const unsigned char*>(data.data()), (int)data.size()}; !transmission.MergePartialFromCodedStream(&stream) )
				THROW( "transmission.MergePartialFromCodedStream returned false" );
			HandleRequest( move(transmission), move(msg) );
		}
		catch( const IException& e )
		{
			_pWebSend->Push( "Request Failed", e, {msg, 0} );
		}
	}
	α ReceiveOptions( SessionKey s, Proto::Requests::RequestOptions o )noexcept->Task;
#pragma warning(disable:4456)
	α WebRequestWorker::HandleRequest( Proto::Requests::RequestTransmission&& transmission, SessionKey&& s )noexcept->void
	{
		while( transmission.messages().size() )
		{
			auto pMessage = sp<Proto::Requests::RequestUnion>( transmission.mutable_messages()->ReleaseLast() );
			auto& message = *pMessage;
			if( message.has_string_request() )
				Receive( message.string_request().type(), move(*message.mutable_string_request()->mutable_name()), {s, message.string_request().id()} );
			else if( message.has_options() )
				ReceiveOptions( s, message.options() );
			else if( message.has_flex_executions() )
				ReceiveFlex( s, message.flex_executions() );
			else if( message.has_edit_watch_list() )
				WatchListData::Edit( message.edit_watch_list().file(), ARG(message.edit_watch_list().id()) );
			else if( auto p=message.has_blockly() ? message.mutable_blockly() : nullptr; p )
			{
				LOG( "({}.{})Blockly Request( size={} )"sv, s.SessionId, p->id(), p->message().size() );
				_pBlocklyWorker->Push( { {s, p->id()}, up<string>{p->release_message()} } );
			}
			else if( message.has_std_dev() )
				ReceiveStdDev( message.std_dev().contract_id(), message.std_dev().days(), message.std_dev().start(), ARG(message.std_dev().id()) );
			else if( auto p = message.has_reddit() ? message.mutable_reddit() : nullptr; p )
				Reddit::Search( move(*p->mutable_symbol()), move(*p->mutable_sort()), make_unique<ProcessArg>(move(s), p->id(), _pWebSend) );
			else
			{
				bool handled = false;
				if( message.has_generic_requests() )
					handled = ReceiveRequests( s, message.generic_requests() );
				if( !handled )
					_pTwsSend->Push( pMessage, s );
			}
		}
	}

	α WebRequestWorker::Receive( ERequests type, string&& name, const ClientKey& arg )noexcept->void
	{
		if( type==ERequests::Query )
		{
			auto p = make_unique<Proto::Results::StringResult>(); p->set_id( arg.ClientId ); p->set_type( EResults::Query );
			MessageType msg;
			try
			{
				var result = DB::Query( name, arg.UserId );//TODO make async
				if( !result.is_null() )
				{
					p->set_value( result.dump() );
					//DBG( "result={}"sv, p->value() );
					//DBG( p->value() );
				}
				msg.set_allocated_string_result( p.release() );
				_pWebSend->Push( move(msg), arg.SessionId );
			}
			catch( const json::exception& e )
			{
				Logging::Log( Logging::Message(ELogLevel::Debug, e.what()) );
				_pWebSend->PushError( "Could not parse query", arg );
			}
			catch( const IException& e )
			{
				_pWebSend->Push( "Query failed", e, arg );
			}
		}
		else if( type==ERequests::GoogleLogin )//https://ncona.com/2015/02/consuming-a-google-id-token-from-a-server/
		{
			try
			{
				/*
		var parts = StringUtilities::Split( name, '.' );
		for( var& part : parts )
			DBG( part );
		var header = json::parse( Ssl::Decode64(parts[0]) );//{"alg":"RS256","kid":"fed80fec56db99233d4b4f60fbafdbaeb9186c73","typ":"JWT"}
		DBG( header.dump() );
		var bodyBuf = Ssl::Decode64( parts[1] );
		DBG( bodyBuf );

		//var encryptedBuf = Ssl::Decode64( parts[2] );
		//DBG( encryptedBuf );

/ *	json jOpenidConfiguration = json::parse( Ssl::Get<string>("accounts.google.com", "/.well-known/openid-configuration") );
		var pJwksUri = jOpenidConfiguration.find( "jwks_uri" ); THROW_IF( pJwksUri==jOpenidConfiguration.end(), Exception("Could not find jwks_uri in '{}'", jOpenidConfiguration.dump()) );
		var uri = pJwksUri->get<string>(); THROW_IF( !uri.starts_with("https://www.googleapis.com"), Exception("Wrong target:  '{}'", uri) );
		var jwks = json::parse( Ssl::Get<string>( "www.googleapis.com", uri.substr(sizeof("https://www.googleapis.com")-1)) );
		var pKid = header.find( "kid" ); THROW_IF( pKid== header.end(), Exception("Could not find kid in header {}", header.dump()) );
		var pKeys = jwks.find( "keys" );  THROW_IF( pKeys==jwks.end(), Exception("Could not find pKeys in jwks {}", jwks.dump()) );
		json foundKey;
		for( var& key : *pKeys )
		{
			if( key["kid"].get<string>()==pKid->get<string>() )
			{
				foundKey = key;
				break;
			}
			break;
		}
		THROW_IF( foundKey.is_null(), Exception("Could not find key '{}' in: '{}'", pKid->get<string>(), pKeys->dump()) );
		var alg = foundKey["alg"].get<string>();
		// var exponent = foundKey["e"].get<string>();
		// var modulus = foundKey["n"].get<string>();
		var exponent = "AQAB";
		var modulus = "rIVm3h1WGbvKjmvzrpwPFeyAWIeP3W87z-C9k0YarePIF0Y77KgaMB83cVv5Hp85Che-Z_nb_y0kBhrOha4_q_6gFEOhyz8PUZSzdY2zkhX8Dci-vic9HulL5cFWjDGPXwekHLm_EmXkPkKu7-6nbkxmwcVQMGX2lEeawCqqNmk=";
		//DBG( "e={} n={}"sv, exponent, modulus );

		Ssl::Verify( modulus, exponent, parts[1], parts[2] );

		var header = JSON.parse(headerBuf.toString());
		var body = JSON.parse(bodyBuf.toString());*/
				if( auto pEmail = Settings::Get<string>("um/user.email"); pEmail )
				{
					USE(WebCoSocket::Instance())SetLogin( arg, EAuthType::Google, *pEmail, true, Settings::Get<string>("um/user.name").value_or(""), "", Clock::now()+std::chrono::hours(100 * 24), "key" );
				}
				else
				{
					var token = Ssl::Get<Google::TokenInfo>( "oauth2.googleapis.com", format("/tokeninfo?id_token={}", name) ); //TODO make async, or use library
					THROW_IF( token.Aud!=Settings::Get<string>("GoogleAuthClientId"), "Invalid client id" );
					THROW_IF( token.Iss!="accounts.google.com" && token.Iss!="https://accounts.google.com", "Invalid iss" );
					var expiration = Clock::from_time_t( token.Expiration ); THROW_IF( expiration<Clock::now(), "token expired" );
					USE(WebCoSocket::Instance())SetLogin( arg, EAuthType::Google, token.Email, token.EmailVerified, token.Name, token.PictureUrl, expiration, name );
				}
				// var expiration = Clock::now()+std::chrono::hours(100 * 24);
			}
			catch( const std::exception& e )
			{
				Logging::Log( Logging::Message(ELogLevel::Warning, e.what()) );
				_pWebSend->PushError( "Authorization failed", arg );
			}
		}
		else if( type==ERequests::WatchList )
			WatchListData::SendList( name, {arg, _pWebSend} );
		else if( type==ERequests::DeleteWatchList )
			WatchListData::Delete( name, {arg, _pWebSend} );
		else if( type==ERequests::WatchList )
			WatchListData::SendList( name, {arg, _pWebSend} );
		else if( type==ERequests::Tweets )
			Twitter::Search( name, {arg, _pWebSend} );
		else if( type==ERequests::RedditBlock )
			Reddit::Block( move(name), mu<ProcessArg>(arg, _pWebSend) );
		//else if( type==ERequests::Investors )
//			EdgarRequests::Investors( name, {arg, _pWebSend} );
	}

/*	α WebRequestWorker::ReceiveRest( const ClientKey& arg, Proto::Requests::ERequests type, sv url, sv item )noexcept->void
	{
		if( UM::IsTarget(url) )
		{
			if( type==ERequests::RestGet )
				UM::Get( url, arg.UserId );
			else if( type==ERequests::RestDelete )
				UM::Delete( url, arg.UserId );
			else if( type==ERequests::RestPatch )
				UM::Patch( url, arg.UserId, item );
			else if( type==ERequests::RestPost )
				UM::Post( url, arg.UserId, item );
		}
	}*/

	bool WebRequestWorker::ReceiveRequests( const SessionKey& s, const Proto::Requests::GenericRequests& r )noexcept
	{
		bool handled = true;
		var t = r.type();
		if( t==ERequests::RequsetPrevOptionValues )
			PreviousDayValues( r.ids(), ARG(r.id()) );
		else if( t==ERequests::RequestFundamentalData )
			RequestFundamentalData( r.ids(), {s, r.id()} );
		else if( t==ERequests::Portfolios )
			WatchListData::SendLists( true, ARG(r.id()) );
		else if( t==ERequests::WatchLists )
			WatchListData::SendLists( false, ARG(r.id()) );
		else if( t==ERequests::Filings || t==ERequests::Investors )
		{
			var contractId = r.ids().size()==1 ? r.ids()[0] : 0;
			if( !contractId )
				_pWebSend->Push( "Error in request", Exception{SRCE_CUR, ELogLevel::Debug, "ids sent: {} expected 1."sv, r.ids().size()}, {{s}, r.id()} );
			else
			{
				LOG( "({})EdgarRequest( {}, {} )", r.id(), t, contractId );
				var contractId = r.ids()[0];
				if( t==ERequests::Filings )
					EdgarRequests::Filings( contractId, ARG(r.id()) );
				else if( t==ERequests::Investors )
					EdgarRequests::Investors( contractId, ARG(r.id()) );
			}
		}
		else
			handled = false;//WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, request.type(), sessionId );
		return handled;
	}
	α WebRequestWorker::ReceiveFlex( const SessionKey& s, const Proto::Requests::FlexExecutions& req )noexcept->void
	{
		var start = Chrono::BeginningOfDay( Clock::from_time_t(req.start()) );
		var end = Chrono::EndOfDay( Clock::from_time_t(req.end()) );
		LOG( "({}.{})Flex '{}'-'{}' account='{}'"sv, s.SessionId, req.id(), ToIsoString(start), ToIsoString(end), req.account_number() );
		Flex::SendTrades( req.account_number(), start, end, ProcessArg ARG(req.id()) );
	}

	α WebRequestWorker::ReceiveStdDev( ContractPK contractId, double days, DayIndex start, ProcessArg inputArg )noexcept->Task
	{
		try
		{
			var pDetails = ( co_await Tws::ContractDetail(contractId) ).SP<::ContractDetails>();
			var pStats = ( co_await ReqStats(ms<Contract>(*pDetails), days, start) ).SP<StatCount>();
			auto p = new Proto::Results::Statistics(); p->set_request_id(inputArg.ClientId); p->set_count(static_cast<uint32>(pStats->Count)); p->set_average(pStats->Average);p->set_variance(pStats->Variance);p->set_min(pStats->Min); p->set_max(pStats->Max);
			MessageType msg; msg.set_allocated_statistics( p );
			inputArg.Push( move(msg) );
		}
		catch( IException& e )
		{
			inputArg.Push( "Calculation failed", move(e) );
		}
	}

	α ReceiveOptions( SessionKey s, Proto::Requests::RequestOptions o )noexcept->Task
	{
		var underlyingId = o.contract_id(); var clientId = o.id();
		ClientKey client{ {s.SessionId}, underlyingId };
		auto pResults = new Proto::Results::OptionValues{}; pResults->set_id( clientId );
		try
		{
			//TODO see if I have, if not download it, else try getting from tws. (make sure have date.)
			THROW_IF( underlyingId==0, "({}.{}) did not pass a contract id.", s.SessionId, clientId );

			var pDetail = ( co_await Tws::ContractDetail(underlyingId) ).SP<::ContractDetails>();
			var contract = Contract{ *pDetail }; THROW_IF( contract.SecType==SecurityType::Option, "({}.{})Passed in option contract ({})'{}', expected underlying", s.SessionId, clientId, underlyingId, contract.LocalSymbol );

			constexpr std::array<sv,4> longOptions{ "TSLA", "GLD", "SPY", "QQQ" }; THROW_IF( std::find(longOptions.begin(), longOptions.end(), contract.Symbol)!=longOptions.end() && !o.start_expiration(), "({}.{})ReceiveOptions request for '{}' specified no date.", s.SessionId, clientId, contract.Symbol );

			if( (o.security_type() & SecurityRight::Call) )
				( co_await OptionData::Load(pDetail, o.start_expiration(), o.end_expiration(), SecurityRight::Call, o.start_srike(), o.end_strike(), pResults) ).CheckError();
			if( (o.security_type() & SecurityRight::Put) )
				( co_await OptionData::Load(pDetail, o.start_expiration(), o.end_expiration(), SecurityRight::Put, o.start_srike(), o.end_strike(), pResults) ).CheckError();

			if( pResults->option_days_size() )
				WebSendGateway::PushS( ToMessage(pResults), s.SessionId );
			else
				WebSendGateway::PushErrorS( "No previous dates found", {{s.SessionId}, underlyingId} );
		}
		catch( const IException& e ){ delete pResults; WebSendGateway::PushS("Retreive Options failed", e, client); }
	}

	α WebRequestWorker::RequestFundamentalData( google::protobuf::RepeatedField<google::protobuf::int32> contractIds, ClientKey s )noexcept->Task
	{
		for( var contractId : contractIds )
		{
			try
			{
				var tick = ( co_await TickManager::Ratios(contractId) ).UP<Tick>();
				WebSendGateway::PushS( ToRatioMessage(tick->Ratios(), s.ClientId), s.SessionId );
				// auto fundamentals = tick.Ratios();
				// auto pRatios = make_unique<Proto::Results::Fundamentals>();
				// pRatios->set_request_id( web.ClientId );
				// for( var& [name,value] : fundamentals )
				// 	(*pRatios->mutable_values())[name] = value;
				// MessageType msg; msg.set_allocated_fundamentals( pRatios.release() );
				// web.Push( move(msg) );
			}
			catch( const IException& e )
			{
				WebSendGateway::PushS( format("Request fundamentals failed - {}", contractId), e, s );
			}
		}
		WebSendGateway::PushS( EResults::MultiEnd, s );
	}
}