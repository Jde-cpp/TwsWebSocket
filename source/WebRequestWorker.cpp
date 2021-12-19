#include "WebRequestWorker.h"
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
	static const LogTag& _logLevel = Logging::TagLevel( "app.webRequests" );
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
#define ARG(x) {{{session}, x}, _pWebSend}
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
#pragma warning(disable:4456)
	α WebRequestWorker::HandleRequest( Proto::Requests::RequestTransmission&& transmission, SessionKey&& session )noexcept->void
	{
		while( transmission.messages().size() )
		{
			auto pMessage = sp<Proto::Requests::RequestUnion>( transmission.mutable_messages()->ReleaseLast() );
			auto& message = *pMessage;
			if( message.has_string_request() )
				Receive( message.string_request().type(), move(*message.mutable_string_request()->mutable_name()), {session, message.string_request().id()} );
			else if( message.has_options() )
				ReceiveOptions( session, message.options() );
			else if( message.has_flex_executions() )
				ReceiveFlex( session, message.flex_executions() );
			else if( message.has_edit_watch_list() )
				WatchListData::Edit( message.edit_watch_list().file(), ARG(message.edit_watch_list().id()) );
			else if( auto p=message.has_blockly() ? message.mutable_blockly() : nullptr; p )
				_pBlocklyWorker->Push( { {session, p->id()}, up<string>{p->release_message()} } );
			else if( message.has_std_dev() )
				ReceiveStdDev( message.std_dev().contract_id(), message.std_dev().days(), message.std_dev().start(), ARG(message.std_dev().id()) );
			else if( auto p = message.has_reddit() ? message.mutable_reddit() : nullptr; p )
				Reddit::Search( move(*p->mutable_symbol()), move(*p->mutable_sort()), make_unique<ProcessArg>(move(session), p->id(), _pWebSend) );
			else
			{
				bool handled = false;
				if( message.has_generic_requests() )
					handled = ReceiveRequests( session, message.generic_requests() );
				if( !handled )
					_pTwsSend->Push( pMessage, session );
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
				if( auto pEmail = Settings::TryGet<string>("um/user.email"); pEmail )
				{
					USE(WebCoSocket::Instance())SetLogin( arg, EAuthType::Google, *pEmail, true, Settings::TryGet<string>("um/user.name").value_or(""), "", Clock::now()+std::chrono::hours(100 * 24), "key" );
				}
				else
				{
					var token = Ssl::Get<Google::TokenInfo>( "oauth2.googleapis.com", format("/tokeninfo?id_token={}", name) ); //TODO make async, or use library
					THROW_IF( token.Aud!=Settings::TryGet<string>("GoogleAuthClientId"), "Invalid client id" );
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

	bool WebRequestWorker::ReceiveRequests( const SessionKey& session, const Proto::Requests::GenericRequests& request )noexcept
	{
		bool handled = true;
		if( request.type()==ERequests::RequsetPrevOptionValues )
			PreviousDayValues( request.ids(), ARG(request.id()) );
		else if( request.type()==ERequests::RequestFundamentalData )
			RequestFundamentalData( request.ids(), {session, request.id()} );
		else if( request.type()==ERequests::Portfolios )
			WatchListData::SendLists( true, ARG(request.id()) );
		else if( request.type()==ERequests::WatchLists )
			WatchListData::SendLists( false, ARG(request.id()) );
		else if( request.type()==ERequests::Filings || request.type()==ERequests::Investors )
		{
			if( request.ids().size()!=1 )
				_pWebSend->Push( "Error in request", Exception{SRCE_CUR, ELogLevel::Debug, "ids sent: {} expected 1."sv, request.ids().size()}, {{session}, request.id()} );
			else
			{
				var contractId = request.ids()[0];
				if( request.type()==ERequests::Filings )
					EdgarRequests::Filings( request.ids()[0], ARG(request.id()) );
				else if( request.type()==ERequests::Investors )
					EdgarRequests::Investors( contractId, ARG(request.id()) );
			}
		}
		else
			handled = false;//WARN( "Unknown message '{}' received from '{}' - not forwarding to tws."sv, request.type(), sessionId );
		return handled;
	}
	α WebRequestWorker::ReceiveFlex( const SessionKey& session, const Proto::Requests::FlexExecutions& req )noexcept->void
	{
		var start = Chrono::BeginningOfDay( Clock::from_time_t(req.start()) );
		var end = Chrono::EndOfDay( Clock::from_time_t(req.end()) );
		std::thread( [web=ProcessArg ARG(req.id()), start, end, accountNumber=req.account_number(), pWebSend=_pWebSend]()
		{
			Flex::SendTrades( accountNumber, start, end, web );
		}).detach();
	}

	α WebRequestWorker::ReceiveStdDev( ContractPK contractId, double days, DayIndex start, ProcessArg inputArg )noexcept->Task2
	{
		try
		{
			var pContract = ( co_await Tws::ContractDetails(contractId) ).Get<const Contract>();
			var pStats = ( co_await ReqStats(pContract, days, start) ).Get<StatCount>();
			auto p = new Proto::Results::Statistics(); p->set_request_id(inputArg.ClientId); p->set_count(static_cast<uint32>(pStats->Count)); p->set_average(pStats->Average);p->set_variance(pStats->Variance);p->set_min(pStats->Min); p->set_max(pStats->Max);
			MessageType msg; msg.set_allocated_statistics( p );
			inputArg.Push( move(msg) );
		}
		catch( IException& e )
		{
			inputArg.Push( "Calculation failed", move(e) );
		}
	}

	α WebRequestWorker::ReceiveOptions( const SessionKey& session, const Proto::Requests::RequestOptions& params )noexcept->void
	{
		std::thread( [params, web=ProcessArg ARG(params.id())]()
		{
			Threading::SetThreadDscrptn( "ReceiveOptions" );
			var underlyingId = params.contract_id();
			try
			{
				//TODO see if I have, if not download it, else try getting from tws. (make sure have date.)
				if( underlyingId==0 )
					THROW( "({}.{}) did not pass a contract id.", web.SessionId, web.ClientId );
				var pDetails = _sync.ReqContractDetails( underlyingId ).get(); THROW_IF( pDetails->size()!=1 , "({}.{}) '{}' had '{}' contracts", web.SessionId, web.ClientId, underlyingId, pDetails->size() );
				var contract = Contract{ pDetails->front() };
				if( contract.SecType==SecurityType::Option )
					THROW( "({}.{})Passed in option contract ({})'{}', expected underlying", web.SessionId, web.ClientId, underlyingId, contract.LocalSymbol );

				const std::array<sv,4> longOptions{ "TSLA", "GLD", "SPY", "QQQ" };
				if( std::find(longOptions.begin(), longOptions.end(), contract.Symbol)!=longOptions.end() && !params.start_expiration() )
					THROW( "({}.{})ReceiveOptions request for '{}' specified no date.", web.SessionId, web.ClientId, contract.Symbol );

				auto pResults = make_unique<Proto::Results::OptionValues>(); pResults->set_id( params.id() );
				sp<Proto::Results::ExchangeContracts> pOptionParams;
				auto loadRight = [&]( SecurityRight right )
				{
					auto fetch = [&]( DayIndex expiration )noexcept(false)
					{
						var ibContract = TwsClientCache::ToContract( contract.Symbol, expiration, right, params.start_srike() && params.start_srike()==params.end_strike() ? params.start_srike() : 0 );
						auto pContracts = _sync.ReqContractDetails( ibContract ).get(); THROW_IF( pContracts->size()<1, "'{}' - '{}' {} has {} contracts", contract.Symbol, DateTime{Chrono::FromDays(expiration)}.DateDisplay(), ToString(right), pContracts->size() );
						var start = params.start_srike(); var end = params.end_strike();
						if( !ibContract.strike && (start!=0 || end!=0) )//remove contracts outside bounds?
						{
							auto pContracts2 = make_shared<vector<ContractDetails>>();
							for( var& details : *pContracts )
							{
								if( (start==0 || start<=details.contract.strike) && (end==0 || end>=details.contract.strike) )
									pContracts2->push_back( details );
							}
							pContracts = pContracts2;
						}
						var valueDay = OptionData::LoadDiff( contract, *pContracts, *pResults );
						if( !valueDay )
						{
							flat_map<double,ContractPK> sorted;
							for( var& details : *pContracts )
								sorted.emplace( details.contract.strike, details.contract.conId );

							auto pExpiration = pResults->add_option_days();
							pExpiration->set_is_call( right==SecurityRight::Call );
							pExpiration->set_expiration_days( expiration );
							for( var& [strike,contractId] : sorted )
							{
								auto pValue = pExpiration->add_values();
								pValue->set_id( contractId );
								//DBG( "strike = '{}'"sv, details.contract.strike );
								pValue->set_strike( static_cast<float>(strike) );
							}
						}
						return valueDay;
					};
					if( params.start_expiration()==params.end_expiration() )
						pResults->set_day( fetch(params.start_expiration()) );
					else
					{
						if( !pOptionParams )
							pOptionParams = Future<Proto::Results::ExchangeContracts>( Tws::SecDefOptParams(underlyingId,true) ).get();
						var end = params.end_expiration() ? params.end_expiration() : std::numeric_limits<DayIndex>::max();
						for( var expiration : pOptionParams->expirations() )
						{
							if( expiration<params.start_expiration() || expiration>end )
								continue;
							try
							{
								pResults->set_day( fetch(expiration) );//sets day multiple times...
							}
							catch( IBException& e )
							{
								if( e.Code!=200 )//No security definition has been found for the request
									throw move(e);
							}
						}
					}
				};
				if( (params.security_type() & SecurityRight::Call) )
					loadRight( SecurityRight::Call );
				if( (params.security_type() & SecurityRight::Put) )
					loadRight( SecurityRight::Put );

				if( pResults->option_days_size() )
				{
					MessageType msg; msg.set_allocated_options( pResults.release() );
					web.Push( move(msg)  );
				}
				else
					web.WebSendPtr->PushError( "No previous dates found", {{web.SessionId}, underlyingId} );
			}
			catch( const IBException& e ){ web.Push("Retreive Options failed", e); }
			catch( const IException& e ){ web.Push("Retreive Options failed", e); }
		} ).detach();
	}

	α WebRequestWorker::RequestFundamentalData( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ClientKey& key )noexcept->void
	{
		std::thread( [current=PreviousTradingDay(), contractIds, web=ProcessArg{key,_pWebSend}]()
		{
			Threading::SetThreadDscrptn( "RatioReq" );
			for( var contractId : contractIds )
			{
				try
				{
					//var pDetails = _sync.ReqContractDetails( contractId ).get(); THROW_IF( pDetails->size()!=1, IBException(format("{} has {} contracts", contractId, pDetails->size()), -1) );
					var tick = TickManager::Ratios( contractId ).get();
					auto fundamentals = tick.Ratios();
					auto pRatios = make_unique<Proto::Results::Fundamentals>();
					pRatios->set_request_id( web.ClientId );
					for( var& [name,value] : fundamentals )
						(*pRatios->mutable_values())[name] = value;
					MessageType msg; msg.set_allocated_fundamentals( pRatios.release() );
					web.Push( move(msg) );
				}
				catch( const IException& e )
				{
					return web.Push( "Request fundamentals failed", e );
				}
			}
			web.Push( EResults::MultiEnd );
		} ).detach();
	}
}