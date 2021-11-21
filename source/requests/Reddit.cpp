#include "Reddit.h"
#include "../WebRequestWorker.h"
#include "../../../Framework/source/io/tinyxml2.h"
#include "../../../Framework/source/db/Database.h"
#include "../../../Ssl/source/SslCo.h"

namespace Jde
{
#pragma region Defines
	using namespace Markets::TwsWebSocket;
	using namespace tinyxml2;
	using Markets::Proto::Results::RedditEntries;
#define var const auto
#pragma endregion
	α Reddit::Search( string&& symbol, string&& sort, up<Markets::TwsWebSocket::ProcessArg> arg )noexcept->Coroutine::Task2
	{
		try
		{
			var pXml = ( co_await Ssl::SslCo::Get("www.reddit.com", format("/r/wallstreetbets/search.xml?q=${}&restrict_sr=on&limit=100&sort={}", symbol, sort.size() ? sort : "hot")) ).Get<string>();//TODOExample: User-Agent: android:com.example.myredditapp:v1.2.3 (by /u/kemitche)
			auto pResults = make_unique<RedditEntries>(); pResults->set_request_id( arg->ClientId );

			tinyxml2::XMLDocument doc{ *pXml }; var pRoot = doc.FirstChildElement( "feed" ); CHECK( pRoot );
			TRY( pResults->set_update_time((uint32)DateTime{pRoot->TryChildText("updated")}.TimeT()) );
			var pBlockedUsers = ( co_await DB::SelectSet<string>("select name from rdt_handles where blocked=1", {}, "rdt_blocked") ).Get<flat_set<string>>();

			for( auto pItem=pRoot->FirstChildElement("entry"); pItem; pItem = pItem->NextSiblingElement("entry") )
			{
				auto p = pResults->add_values();
				p->set_id( string{pItem->TryChildText("id")} );
				p->set_title( string{pItem->TryChildText("title")} );
				sv content = pItem->TryChildText( "content" );
				constexpr sv txt{ "<a href=\"https://www.reddit.com/user/" };
				string user;
				if( uint i=content.find(txt)+txt.size(); i!=string::npos+txt.size() && i<content.size() )
				{
					user = content.substr( i, content.substr(i).find('"') );
					if( pBlockedUsers->contains(user) )
						continue;
					//p->set_user( user );
				}
				p->set_content( string{content} );
				p->set_link( string{pItem->TryChildAttribute("link", "href")} );
				p->set_category( string{pItem->TryChildAttribute("category", "term")} );
				TRY( p->set_published((uint32)DateTime{pItem->TryChildText("published")}.TimeT()) );
			}
			MessageType m; m.set_allocated_reddit( pResults.release() );
			arg->Push( move(m) );
		}
		catch( const IException& e )
		{
			arg->Push( e );
		}
	}
	α Reddit::Block( string&& user, up<Markets::TwsWebSocket::ProcessArg> pArg )noexcept->Coroutine::Task2
	{
		try
		{
			(co_await *DB::ExecuteProcCo("rdt_handle_block(?)", {user}) ).Get<uint>();
			pArg->Push( EResults::Success );
		}
		catch( IException& e )
		{
			pArg->Push( move(e) );
		}
	}
}