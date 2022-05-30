#include "News.h"
#include "../../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../../MarketLibrary/source/client/TwsClientCo.h"
#include "../WebRequestWorker.h"
#include "../../../Framework/source/io/ProtoUtilities.h"
#include <jde/io/tinyxml2.h>
#include "../../../Ssl/source/SslCo.h"

#define _sync TwsClientSync::Instance()

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	using namespace Xml;

	α News::RequestArticle( str providerCode, str articleId, ProcessArg arg )noexcept->Task
	{
		auto p = ( co_await Tws::NewsArticle(providerCode, articleId) ).UP<Proto::Results::NewsArticle>();
		p->set_request_id( arg.ClientId );
		MessageType m; m.set_allocated_news_article( new Proto::Results::NewsArticle(*p) );
		arg.Push( move(m) );
	}

	α News::RequestProviders( ProcessArg arg )noexcept->Task
	{
		try
		{
			auto pProviders = (co_await Tws::NewsProviders()).SP<map<string,string>>();
			auto pMap = mu<Proto::Results::StringMap>();
			pMap->set_result( EResults::NewsProviders );
			for( var& [code,name] : *pProviders )
				(*pMap->mutable_values())[code] = name;

			MessageType type; type.set_allocated_string_map( pMap.release() );
			arg.Push( move(type) );
		}
		catch( const IException& e )
		{
			arg.Push( "Requesting providers failed", e );
		}
	}

	News::THistoricalResult News::RequestHistorical( ContractPK contractId, google::protobuf::RepeatedPtrField<string> providerCodes, uint limit, time_t start, time_t end, ProcessArg arg )noexcept
	{
		try
		{
			var pDetails = (co_await Tws::ContractDetail( contractId )).SP<::ContractDetails>();// if( variant.index()==1 ) std::rethrow_exception( get<1>(variant) ); var pContract = move( get<0>(variant) );
			var& c{ pDetails->contract };
			auto pWait = Tws::HistoricalNews( c.conId, IO::Proto::ToVector(providerCodes), limit, Clock::from_time_t(start), Clock::from_time_t(end) );
			auto pGoogle = (co_await Google( c.symbol) ).UP<vector<sp<Proto::Results::GoogleNews>>>();
			auto pHistorical = (co_await pWait).UP<Proto::Results::NewsCollection>();

			auto pResults = mu<Proto::Results::NewsCollection>( *pHistorical );
			pResults->set_request_id( arg.ClientId );
			for( var& p : *pGoogle )
				*pResults->add_google() = *p;

			MessageType type; type.set_allocated_news( move(pResults.release()) );
			arg.Push( move(type) );
		}
		catch( IException& e )
		{
			arg.Push( "requesting historical failed", e );
		}
	}

	α News::Google( const CIString& symbol )noexcept->TGoogleCoResult{ return TGoogleCoResult{ [=](auto h)->TGoogleAsync
	{
		try
		{
			auto query = format( "search?q={}&hl=en-US&gl=US&ceid=US%3Aen", symbol );
			if( symbol=="SPY" )
				query = "topics/CAAqJAgKIh5DQkFTRUFvS0wyMHZNSE4zYm5OMGNCSUNaVzRvQUFQAQ?hl=en-US&gl=US&ceid=US:en";
			else if( symbol=="TSLA" )
				query = "topics/CAAqIggKIhxDQkFTRHdvSkwyMHZNR1J5T1RCa0VnSmxiaWdBUAE?hl=en-US&gl=US&ceid=US%3Aen";
			AwaitResult xml = co_await Ssl::SslCo::Get( "news.google.com", format("/rss/{}", query) );
			auto pXml = xml.UP<string>();

			auto results = mu<vector<sp<Proto::Results::GoogleNews>>>();
			XMLDocument doc{ *pXml, false }; var pRoot = doc.FirstChildElement( "rss" ); CHECK( pRoot ); var pChannel = pRoot->FirstChildElement( "channel" ); CHECK( pChannel );
			for( auto pItem=pChannel->FirstChildElement("item"); pItem; pItem = pItem->NextSiblingElement("item") )
			{
				auto p = make_shared<Proto::Results::GoogleNews>();
				p->set_title( string{pItem->TryChildText("title")} );
				p->set_link( string{pItem->TryChildText("link")} );
				var pubDateString = pItem->TryChildText( "pubDate" );//Sat, 05 Jun 2021 12:00:00 GMT
				try
				{
					CHECK( (pubDateString.size()==29) ); CHECK( (pubDateString.substr(26)=="GMT") );
					var day = Toε<uint8>( pubDateString.substr(5,2) );
					var month = DateTime::ParseMonth( pubDateString.substr(8,3) );
					var year = Toε<uint16>( pubDateString.substr(12,4) );
					var hour = Toε<uint8>( pubDateString.substr(17,2) );
					var minute = Toε<uint8>( pubDateString.substr(20,2) );
					var second = Toε<uint8>( pubDateString.substr(23,2) );
					p->set_publication_date( (uint32)DateTime{year, month, day, hour, minute, second}.TimeT() );
				}
				catch( const IException& e )
				{
					DBG( "could not parse '{}' - {}", pubDateString, e.what() );
				}

				//
				p->set_description( string{pItem->TryChildText("description")} );
				if( auto pSource=pItem->FirstChildElement("source"); pSource )
				{
					p->set_source_url( string{pSource->Attr("url")} );
					p->set_source( string{pSource->Text()} );
				}
				results->push_back( move(p) );
			}
			h.promise().get_return_object().SetResult( results.release() );
		}
		catch( IException& e )
		{
			h.promise().get_return_object().SetResult( move(e) );
		}
		h.resume();
	}};}
}