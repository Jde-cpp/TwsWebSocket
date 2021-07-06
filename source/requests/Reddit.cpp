#include "Reddit.h"
#include "../WebRequestWorker.h"
#include "../../../Framework/source/io/tinyxml2.h"
#include "../../../Ssl/source/SslCo.h"

namespace Jde
{
#pragma region Defines
	using namespace Markets::TwsWebSocket;
	using namespace tinyxml2;
	using namespace Markets::Proto::Results;
#define var const auto
#pragma endregion
	α Jde::Reddit::Search( string&& symbol, string&& sort, up<Markets::TwsWebSocket::ProcessArg> arg )noexcept->Coroutine::Task2
	{
		try
		{
			var pXml = ( co_await Ssl::SslCo::Get("www.reddit.com", format("/r/wallstreetbets/search.xml?q=${}&restrict_sr=on&limit=100&sort={}", symbol, sort.size() ? sort : "hot")) ).Get<string>();//TODOExample: User-Agent: android:com.example.myredditapp:v1.2.3 (by /u/kemitche)
			auto pResults = make_unique<RedditEntries>(); pResults->set_request_id( arg->ClientId );

			tinyxml2::XMLDocument doc{ *pXml }; var pRoot = doc.FirstChildElement( "feed" ); CHECK( pRoot );
			TRY( pResults->set_update_time(DateTime{pRoot->TryChildText("updated")}.TimeT()) );
			for( auto pItem=pRoot->FirstChildElement("entry"); pItem; pItem = pItem->NextSiblingElement("entry") )
			{
				auto p = pResults->add_values();
				p->set_id( string{pItem->TryChildText("id")} );
				p->set_title( string{pItem->TryChildText("title")} );
				p->set_content( string{pItem->TryChildText("content")} );
				p->set_link( string{pItem->TryChildAttribute("link", "href")} );
				p->set_category( string{pItem->TryChildAttribute("category", "term")} );
				TRY( p->set_published(DateTime{pItem->TryChildText("published")}.TimeT()) );
			}
			MessageType m; m.set_allocated_reddit( pResults.release() );
			arg->Push( move(m) );
		}
		catch( const Exception& e )
		{
			e.Log();
			arg->Push( e );
		}
	}
}