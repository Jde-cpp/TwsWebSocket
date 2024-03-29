﻿#include "Twitter.h"
#include <jde/coroutine/Task.h>
#include <jde/io/Crc.h>
#include <jde/markets/types/proto/results.pb.h>
#include <jde/markets/types/proto/ResultsMessage.h>
#include "../../../Framework/source/Settings.h"
#include "../../../Framework/source/collections/Collections.h"
#include "../../../Framework/source/db/Syntax.h"
#include "../../../Framework/source/io/ProtoUtilities.h"
#include "../../../Ssl/source/Ssl.h"
#include "../../../Ssl/source/SslCo.h"
#include "../../../Framework/source/db/Database.h"

namespace Jde
{
#pragma region Defines
	using namespace Markets::TwsWebSocket;
	using namespace Coroutine;
	α SendAuthors( set<uint> authors, ProcessArg arg, string bearerToken, uint callCount )->Coroutine::Task;
	using Markets::Proto::Results::Tweets; using ProtoTweet=Markets::Proto::Results::Tweet;
	using Markets::Proto::Results::TweetAuthors; using Markets::Proto::Results::TweetAuthor;
	static const LogTag& _logLevel = Logging::TagLevel( "app-tweet" );

	std::atomic<bool> CanBlock=true;//app may not have permissions
#define var const auto
#define FROM_JSON( memberString, member ) if( var p=j.find(memberString); p!=j.end() ) p->get_to( o.member )
#define FROM_JSON64( memberString, member ) if( var p=j.find(memberString); p!=j.end() ){ string s; p->get_to( s ); o.member=Toε<uint>(s); }
	namespace Twitter
	{
		struct TwitterSettings
		{
#define $(x) Settings::Envɛ(string{base}+x)
			TwitterSettings()noexcept(false):
				ApiSecretKey{ $("apiSecretKey") },
				ApiKey{ $("apiKey") },
				AccessToken{ $("accessToken") },
				AccessTokenSecret{ $("accessTokenSecret") },
				BearerToken{ $("bearerToken") },
				MinimumLikes{ Settings::Get<uint8>(string{base}+"minimumLikes").value_or(0) }
			{}
#undef $
			α CanBlock()const noexcept{ return ApiSecretKey.size() && ApiKey.size() && AccessToken.size() && AccessTokenSecret.size(); }
			string ApiSecretKey;
			string ApiKey;
			string AccessToken;
			string AccessTokenSecret;
			string BearerToken;
			uint8 MinimumLikes{10};
		private:
			constexpr static const sv base = "twitter/";

		};
	}
#pragma endregion
#pragma region Classes
	struct User
	{
		static void from_json( const nlohmann::json& j, User& o )noexcept(false)
		{
			FROM_JSON64( "id_str", Id );
			FROM_JSON( "profile_image_url", ProfileImageUrl );
			FROM_JSON( "screen_name", ScreenName );
		}
		uint Id;
		string ProfileImageUrl;
		string ScreenName;
	};
	struct Meta
	{
		static void from_json( const nlohmann::json& j, Meta& o )noexcept
		{
			FROM_JSON( "next_token", NextToken );
		}

		string NextToken;
	};
	struct PublicMetrics
	{
		static void from_json( const nlohmann::json& j, PublicMetrics& o )noexcept
		{
			FROM_JSON( "retweet_count", Retweet );
			FROM_JSON( "reply_count", Reply );
			FROM_JSON( "like_count", Like );
			FROM_JSON( "quote_count", Quote );
		}
		uint32 Retweet{0};
		uint32 Reply{0};
		uint32 Like{0};
		uint32 Quote{0};
	};
	struct Tweet
	{
		static void from_json( const nlohmann::json& j, Tweet& o )noexcept
		{
			FROM_JSON64( "id", Id );
			FROM_JSON64( "author_id", AuthorId );
			FROM_JSON( "text", Text );
			if( var p=j.find("created_at"); p!=j.end() )
				o.CreatedAt = DateTime( p->get<string>() );

			if( var p=j.find("public_metrics"); p!=j.end() )
				PublicMetrics::from_json( *p, o.Metrics );
		}
		static bool isDigit( char c )noexcept{ return c>='0' && c<='9'; }//microsoft asserts if less than zero.
		vector<sv> Tags( const CIString& excluded )const noexcept
		{
			vector<sv> results; results.reserve( 8 );
			sv w;
			for( uint i=0; i<Text.size(); i+=w.size() )
			{
				w = Str::NextWord( sv{Text.data()+i,Text.size()-i} );
				if( w.size()>1 && ((w[0]=='$' && !isDigit(w[1])) || w[0]=='#') && CIString{w}.find(excluded)==string::npos )
				{
					if( w.ends_with(',') )
						w = sv{w.data(), w.size()-1};
					results.emplace_back( w );
				}
			}
			return results;
		}
		uint Id;
		uint AuthorId;
		TimePoint CreatedAt;
		PublicMetrics Metrics;
		string Text;
	};
	struct Recent
	{
		static void from_json( const nlohmann::json& j, Recent& o )noexcept(false)
		{
			if( var p=j.find("meta"); p!=j.end() )
				Meta::from_json( *p, o.MetaData );
			if( var p=j.find("data"); p!=j.end() )
			{
				for( var& t : *p )
					Tweet::from_json( t, o.Tweets.emplace_back() );
			}
		}
		vector<Tweet> Tweets;
		Meta MetaData;
	};
#pragma endregion
	α Block2( uint userId, const Twitter::TwitterSettings& settings )noexcept{ return Coroutine::AsyncAwait{ [=](auto h)->Task
	{
		var once = std::to_string( userId );
		var url = "https://api.twitter.com/1.1/blocks/create.json"sv;
		var time = std::time( nullptr );
		std::ostringstream os;
		os << "POST&" << Ssl::Encode(url) << "&"
			<< "oauth_consumer_key%3D" << Ssl::Encode( settings.ApiKey )
			<< "%26oauth_nonce%3D" << once
			<< "%26oauth_signature_method%3DHMAC-SHA1"
			<< "%26oauth_timestamp%3D" << time
			<< "%26oauth_token%3D" << Ssl::Encode( settings.AccessToken )
			<< "%26oauth_version%3D1.0"
			<< "%26skip_status%3D1"
			<< "%26user_id%3D" << userId;
		ostringstream osAuth;
		osAuth << "OAuth "
				<< "oauth_consumer_key=\"" << settings.ApiKey << "\","
				<< "oauth_token=\"" << settings.AccessToken << "\","
				<< "oauth_signature_method=\"HMAC-SHA1\","
				<< "oauth_timestamp=\"" << time << "\","
				<< "oauth_nonce=\"" << once << "\","
				<< "oauth_version=\"1.0\","
				<< "oauth_signature=\"" << Ssl::Encode( Ssl::RsaSign(os.str(), format("{}&{}", settings.ApiSecretKey, settings.AccessTokenSecret)) ) << "\"";//https://datatracker.ietf.org/doc/html/rfc5849#section-3.4

		h.promise().get_return_object().SetResult( co_await Ssl::SslCo::SendEmpty("api.twitter.com", fmt::vformat(string(url.substr(23))+string("?user_id={}&skip_status=1"), fmt::make_format_args(userId)), osAuth.str()) );
		CanBlock = h.promise().get_return_object().HasError();
		h.resume();
	}};}

	α Twitter::Block( uint id, ProcessArg arg )noexcept->Coroutine::Task
	{
		try
		{
			TwitterSettings settings; THROW_IF( !settings.CanBlock(), "Must specify in settings:  ApiSecretKey, ApiKey, AccessToken, AccessTokenSecret" );
			if( CanBlock )
				( co_await Block2( id, settings) );
			if( !CanBlock )//above can update
				( co_await *DB::ExecuteProcCo("twt_user_block(?)", {id}) ).CheckError();
			arg.Push( EResults::Success );
		}
		catch( IException& e )
		{
			arg.Push( "Blocking user failed", e );
		}
	}

	α Twitter::Search( string symbol, ProcessArg arg )noexcept->Coroutine::Task
	{
		var _ = ( co_await CoLockKey( format("Twitter::Search.{}", symbol), true) ).UP<CoLockGuard>();
		set<uint> authors;
		TwitterSettings settings; if( !settings.BearerToken.size() ) co_return arg.PushError( "twitter credentials not set on server" );
		uint callCount{ 0 };

		auto pExisting{ Cache::Get<Tweets>(format("Tweets.{}", symbol)) };

		var existingPath{ IApplication::ApplicationDataFolder()/"tweets"/(symbol+".dat") };
		if( !pExisting )
		{
			auto pFile = IO::Proto::TryLoad<Tweets>( existingPath );
			pExisting = pFile ? ms<Tweets>( *pFile ) : ms<Tweets>();
			if( pFile )
				LOG( "fetched from file {}", pFile->values_size() );
			else if( !fs::exists(existingPath.parent_path()) )
				fs::create_directory( existingPath.parent_path() );
		}
		var now = Clock::now();
		var cutoff = now-20min;
		var likeUpdateCutoff = now-12h;
		LOG( "count={}, LastUpdate={} cutoff={}"sv, pExisting->values_size(), ToIsoString(Clock::from_time_t(pExisting->update_time())), ToIsoString(cutoff) );
		if( pExisting->values_size() && Clock::from_time_t(pExisting->update_time())>cutoff )
		{
			LOG( "Pushing {} previously fetched", pExisting->values_size() );
			arg.Push( Markets::ToMessage( arg.ClientId, new Tweets(*pExisting)) );
			for( auto& t : pExisting->values() )
				authors.emplace( t.author_id() );
			SendAuthors( authors, arg, settings.BearerToken, callCount );
			co_return;
		}
		else
		{
			auto pTemp = ms<Tweets>();
			pTemp->set_update_time( pExisting->update_time() );
			pTemp->set_earliest_time( pExisting->earliest_time() );
			var earliest = std::time(nullptr)-7*24*60*60;
			for( auto& t : pExisting->values() )
			{
				if( t.created_at()>earliest )
					*pTemp->add_values() = t;
			}
			pExisting = pTemp;
		}
		std::multimap<time_t,ProtoTweet*> existing;
		for( auto& t : *pExisting->mutable_values() )
			existing.emplace( t.created_at(), &t );

		var additional = DB::Scaler<string>( "select query from twt_queries where tag=?", {symbol} ).value_or( string{} );
		var prefix = additional.size() ? format("{}%20{}", Ssl::Encode(symbol), Ssl::Encode(additional) ) : Ssl::Encode( symbol );
		constexpr sv suffix = "%20-is:retweet%20lang:en"sv;
		var pExistingIgnoredTags = ( co_await DB::SelectMap<string,uint>("select tag, ignored_count from twt_tags order by 1", "twt_tags") ).SP<flat_map<string,uint>>();
		auto pIgnoredTags = ms<flat_map<string,uint>>( *pExistingIgnoredTags );
		flat_multimap<uint,string> ignoredSorted;
		for_each( pExistingIgnoredTags->begin(), pExistingIgnoredTags->end(), [&ignoredSorted](var& i){ ignoredSorted.emplace(i.second,i.first);} );
		ostringstream osIgnored; const String ciSymbol{ ToIV(symbol) };
		for( auto p=ignoredSorted.rbegin(); p!=ignoredSorted.rend(); ++p )
		{
			var tag = p->second.starts_with("$") || p->second.starts_with("#") ? p->second.substr(1) : p->second;
			if( prefix.size()+suffix.size()+tag.size()+osIgnored.str().size()+4>512 )
				break;
			if( tag.size()>1 && ciSymbol.find(ToIV(tag))==string::npos )
			osIgnored << "%20-" << tag;
		}
		var query = format( "{}{}{}", prefix, osIgnored.str(), suffix );

		LOG( "{} - {}"sv, query, ignoredSorted.size() );
		var pBlockedUsers = ( co_await DB::SelectSet<uint>( "select id from twt_handles where blocked=1", {}, "twt_blocks") ).SP<flat_set<uint>>();
		set<uint> newBlockedUsers;

		var epoch = now-24h;
		auto lastChecked = Clock::from_time_t(pExisting->update_time())>epoch ? Clock::from_time_t(pExisting->update_time()) : epoch;
		LOG( "lastChecked={}"sv, ToIsoString(lastChecked) );
		auto earliest = Clock::from_time_t(pExisting->earliest_time())>epoch ? Clock::from_time_t(pExisting->earliest_time()) : epoch;
		string nextToken;
		var startTime = lastChecked>epoch+12h ? epoch+12h : lastChecked>epoch ? lastChecked : epoch;//update likes over 12 hours, but not over 24 hours.  less than 20minutes, handled above.
		var startTimeUrl = format( "&start_time={}&max_results=100", ToIsoString(startTime) );
		var require$Hash = ciSymbol=="SPY";
		set<uint> sent;
		try
		{
			for( uint i=0; i<10; i = nextToken.size() ? i+1 : 10 )
			{
				++callCount;
				Coroutine::AwaitResult result2 = co_await Ssl::SslCo::Get( "api.twitter.com", format("/2/tweets/search/recent?query={}&tweet.fields=public_metrics,author_id,created_at{}{}", query, startTimeUrl, nextToken), format("Bearer {}", settings.BearerToken) );
				var pResult = result2.UP<string>();
				json j;
				try
				{
					j = nlohmann::json::parse( *pResult );
				}
				catch( const nlohmann::json::exception& e )
				{
					DBG( "json exception - {}\n{}"sv, e.what(), *pResult );
					continue;
				}

				Recent recent;
				Recent::from_json( j, recent );

				vector<ProtoTweet*> toSend;
				for( var& t : recent.Tweets )
				{
					var a = t.AuthorId;
					if( newBlockedUsers.contains(a) || pBlockedUsers->contains(a) )
						continue;
					vector<sv> tags = t.Tags( symbol );
					if( require$Hash && find_if(tags.begin(), tags.end(), [&]( sv& x ){return x.size()==symbol.size()+1 && x.starts_with('$') && ciSymbol==ToIV(x.substr(1));})==tags.end() )
						continue;
					//DBG( "{}", t.Text );
					if( tags.size()>4 )
					{
						newBlockedUsers.emplace( t.AuthorId );
						for( var tag : tags )
							++( pIgnoredTags->try_emplace( string{tag} ).first->second );
						continue;
					}
					if( t.Metrics.Like<settings.MinimumLikes )
						continue;
					var createdAt = Clock::to_time_t( t.CreatedAt );
					auto range = existing.equal_range( createdAt );
					ProtoTweet* p = nullptr;
					for( auto pTime = range.first;p==nullptr && pTime != range.second; ++pTime )
					{
						if( pTime->second->id()==t.Id )
							p = pTime->second;
					}
					if( !p )
					{
						p = existing.emplace( createdAt, pExisting->add_values() )->second; p->set_id( t.Id ); p->set_author_id( t.AuthorId ); p->set_created_at( (uint32)createdAt ); p->set_text( t.Text );
					}
					p->set_retweet( t.Metrics.Retweet ); p->set_reply( t.Metrics.Reply ); p->set_like( t.Metrics.Like ); p->set_quote( t.Metrics.Quote );
					if( t.Metrics.Like>5 )
						toSend.push_back( p );
				}
				if( toSend.size() )
				{
					auto pTweets = mu<Tweets>();
					for( auto p : toSend )
					{
						authors.emplace( p->author_id() );
						sent.emplace( p->id() );
						*pTweets->add_values() = *p;
					}
					arg.Push( Markets::ToMessage(arg.ClientId, pTweets.release()) );
				}
				nextToken = recent.MetaData.NextToken.size() ? format( "&next_token={}", recent.MetaData.NextToken ) : string{};
			}
			lastChecked = now;
			earliest = epoch;
			LOG( "earliest={}, lastChecked={}"sv, ToIsoString(earliest), ToIsoString(lastChecked) );
		}
		catch( IException& e )
		{
			arg.Push( "Fetching tweets failed", e );
		}
		try
		{
			auto pTweets = mu<Tweets>();
			var now_t{ Clock::to_time_t(now) };
			auto cmp = [now_t]( const ProtoTweet* a, const ProtoTweet* b )->bool
			{ 
				return a->like()/(a->created_at()-now_t)<b->like()/(b->created_at()-now_t);
				//return a<b;
			};
			std::set<const ProtoTweet*, decltype(cmp)> sorted( cmp );
			for( var& t : pExisting->values() )
			{
				if( sent.contains(t.id()) )
					continue;
				sorted.emplace( &t );
			}

			for( auto t : sorted )
			{
				if( pTweets->values_size()>300 )
					break;
				*pTweets->add_values() = *t;
				authors.emplace( t->author_id() );
			}

			pTweets->set_update_time( (uint32)Clock::to_time_t(lastChecked) ); pExisting->set_update_time( (uint32)Clock::to_time_t(lastChecked) );
			pTweets->set_earliest_time( (uint32)Clock::to_time_t(earliest) );  pExisting->set_earliest_time( (uint32)Clock::to_time_t(earliest) );
			if( pTweets->values_size() )
				arg.Push( Markets::ToMessage(arg.ClientId, pTweets.release()) );

			if( newBlockedUsers.size() )
			{
				auto pCache = ms<flat_set<uint>>( *pBlockedUsers );
				for( var id : newBlockedUsers )
				{
					if( CanBlock )
						CanBlock = !( co_await Block2( id, settings) ).HasError();
					DB::ExecuteProc( "twt_user_block(?)", {id} );
					pCache->emplace( id );
				}
				Cache::Set( "twt_blocks", pCache );
				for( var& [tag,count2] : *pIgnoredTags )
					DB::ExecuteProc( "twt_tag_ignore(?,?)", {tag,count2} );
				Cache::Set( "twt_tags", pIgnoredTags );
			}
			IO::Proto::Save( *pExisting, existingPath );
			Cache::Set<Tweets>( format("Tweets.{}", symbol), pExisting );
			SendAuthors( authors, arg, settings.BearerToken, callCount );
		}
		catch( IException& e )
		{
			arg.Push( "author implementation failed", e );
		}
		catch( const std::exception& e )
		{
			ERR( "exception:  {}"sv, e.what() );
			arg.PushError( "author implementation failed" );
		}
	}
#pragma region Other
	α SendAuthors( set<uint> authors, ProcessArg arg, string bearerToken, uint callCount )->Coroutine::Task
	{
		auto pAuthorResults = mu<TweetAuthors>();
		if( authors.size() )
		{
			var add = [&authors,&pAuthorResults]( var& row ){ auto p = pAuthorResults->add_values(); p->set_id( row.GetUInt(0) ); p->set_screen_name( row.GetString(1) ); p->set_profile_url( row.GetString(2) ); authors.erase(p->id()); };
			DB::SelectIds( "select id, screen_name, profile_image from twt_handles where id in ", authors, add );
			LOG( "Adding {} users"sv, authors.size() );
			for( var id : authors )
			{
				if( callCount>=300 )
				{
					LOG( "authors.size({})>300 - break;", authors.size() );
					break;
				}
				try
				{
					++callCount;
					auto pResult = ( co_await Ssl::SslCo::Get("api.twitter.com", format("/1.1/users/show.json?user_id={}", id), format("Bearer {}", bearerToken)) ).UP<string>();
					var j = nlohmann::json::parse( *pResult );
					User user;
					User::from_json( j, user );
					//LOG( "twt_user_insert({},{},{})"sv, user.Id, user.ScreenName, user.ProfileImageUrl );
					( co_await *DB::ExecuteProcCo("twt_user_insert(?,?,?)", {user.Id, user.ScreenName, user.ProfileImageUrl}) ).CheckError();
					//DB::ExecuteProc( "twt_user_insert(?,?,?)", {user.Id, user.ScreenName, user.ProfileImageUrl} );
					auto p = pAuthorResults->add_values(); p->set_id( id ); p->set_screen_name( user.ScreenName ); p->set_profile_url( user.ProfileImageUrl );
				}
				catch( const NetException& e )
				{
					const string what{ e.what() };
					if( what.find("User not found.")==string::npos && what.find("User has been suspended.")==string::npos )/*User has been suspended=code=63*/
					{
						if( what.find("Rate limit exceeded") )
							e.Log( format("count={}", callCount) );
						break;
					}
				}
				catch( IException& )
				{
					BREAK;
				}
			}
		}
		arg.Push( Markets::ToMessage(arg.ClientId, pAuthorResults.release()) );
	}
#pragma endregion
}