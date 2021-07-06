#include "News.h"
#include "../../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../../MarketLibrary/source/client/TwsClientCo.h"
#include "../WebRequestWorker.h"
#include "../../../Framework/source/io/ProtoUtilities.h"
#include "../../../Framework/source/io/tinyxml2.h"
#include "../../../Ssl/source/SslCo.h"

#define _sync TwsClientSync::Instance()

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	using namespace tinyxml2;

	α News::RequestArticle( str providerCode, str articleId, ProcessArg arg )noexcept->Task2
	{
		auto p = ( co_await TwsClientCo::NewsArticle(providerCode, articleId) ).Get<Proto::Results::NewsArticle>();
		p->set_request_id( arg.ClientId );
		MessageType m; m.set_allocated_news_article( new Proto::Results::NewsArticle(*p) );
		arg.Push( move(m) );
	}

	α News::RequestProviders( const ProcessArg& arg )noexcept->Task2
	{
		try
		{
			auto pProviders = (co_await TwsClientCo::NewsProviders()).Get<map<string,string>>();
			auto pMap = make_unique<Proto::Results::StringMap>();
			pMap->set_result( EResults::NewsProviders );
			for( var& [code,name] : *pProviders )
				(*pMap->mutable_values())[code] = name;

			MessageType type; type.set_allocated_string_map( pMap.release() );
			arg.Push( move(type) );
		}
		catch( const Exception& e )
		{
			arg.Push( e );
		}
	}

	News::THistoricalResult News::RequestHistorical( ContractPK contractId, google::protobuf::RepeatedPtrField<string> providerCodes, uint limit, time_t start, time_t end, ProcessArg arg )noexcept
	{
		try
		{
			var pContract = (co_await TwsClientCo::ContractDetails( contractId )).Get<Contract>();// if( variant.index()==1 ) std::rethrow_exception( get<1>(variant) ); var pContract = move( get<0>(variant) );

			auto pWait = TwsClientCo::HistoricalNews( pContract->Id, IO::Proto::ToVector(providerCodes), limit, Clock::from_time_t(start), Clock::from_time_t(end) );
			auto pGoogle = (co_await Google( pContract->Symbol) ).Get<vector<sp<Proto::Results::GoogleNews>>>();
			auto pHistorical = (co_await pWait).Get<Proto::Results::NewsCollection>();

			auto pResults = make_unique<Proto::Results::NewsCollection>( *pHistorical );
			pResults->set_request_id( arg.ClientId );
			for( var& p : *pGoogle )
				*pResults->add_google() = *p;

			MessageType type; type.set_allocated_news( move(pResults.release()) );
			arg.Push( move(type) );
		}
		catch( const Exception& e )
		{
			arg.Push( e );
		}
		catch( const std::exception& e )
		{
			arg.Push( e );
		}
	}

	α News::Google( const CIString& symbol )noexcept->TGoogleCoResult{ return TGoogleCoResult{ [=](auto h)->TGoogleAsync
	{
		try
		{
			auto query = format( "search/${}+when:1d&hl=en-US&gl=US&ceid=US:en", symbol );
			if( symbol=="SPY" )
				query = "topics/CAAqJAgKIh5DQkFTRUFvS0wyMHZNSE4zYm5OMGNCSUNaVzRvQUFQAQ?hl=en-US&gl=US&ceid=US:en";
			else if( symbol=="TSLA" )
				query = "topics/CAAqIggKIhxDQkFTRHdvSkwyMHZNR1J5T1RCa0VnSmxiaWdBUAE?hl=en-US&gl=US&ceid=US%3Aen";
			TaskResult xml = co_await Ssl::SslCo::Get( "news.google.com", format("/rss/{}", query) );
			sp<string> pXml = xml.Get<string>();
			//var pXml = (co_await Ssl::CoGet( "news.google.com", format("/rss/search?q=${}+when:1d&hl=en-US&gl=US&ceid=US:en", symbol)) ).Get<string>();

			TGoogleResult results = make_shared<vector<sp<Proto::Results::GoogleNews>>>();
			tinyxml2::XMLDocument doc{ *pXml }; var pRoot = doc.FirstChildElement( "rss" ); CHECK( pRoot ); var pChannel = pRoot->FirstChildElement( "channel" ); CHECK( pChannel );
			for( auto pItem=pChannel->FirstChildElement("item"); pItem; pItem = pItem->NextSiblingElement("item") )
			{
				auto p = make_shared<Proto::Results::GoogleNews>();
				p->set_title( string{pItem->TryChildText("title")} );
				p->set_link( string{pItem->TryChildText("link")} );
				var pubDateString = pItem->TryChildText( "pubDate" );//Sat, 05 Jun 2021 12:00:00 GMT
				try
				{
					CHECK( (pubDateString.size()==29) ); CHECK( (pubDateString.substr(26)=="GMT") );
					var day = Str::To<uint8>( pubDateString.substr(5,2) );
					var month = DateTime::ParseMonth( pubDateString.substr(8,3) );
					var year = Str::To<uint16>( pubDateString.substr(12,4) );
					var hour = Str::To<uint8>( pubDateString.substr(17,2) );
					var minute = Str::To<uint8>( pubDateString.substr(20,2) );
					var second = Str::To<uint8>( pubDateString.substr(23,2) );
					p->set_publication_date( (uint32)DateTime{year, month, day, hour, minute, second}.TimeT() );
				}
				catch( const Exception& e )
				{
					e.Log( pubDateString );
				}

				//
				p->set_description( string{pItem->TryChildText("description")} );
				if( auto pSource=pItem->FirstChildElement("source"); pSource )
				{
					p->set_source_url( string{pSource->AttributeValue("url")} );
					p->set_source( pSource->GetText() );
				}
				results->push_back( move(p) );
			}
			h.promise().get_return_object().SetResult( results );
		}
		catch( const std::exception& e )
		{
			ERR( string{e.what()} );
			h.promise().get_return_object().SetResult( e );
		}
		h.resume();
	}};}
}
