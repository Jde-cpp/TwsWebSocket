#include "WebCoSocket.h"
#include "Flex.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../Framework/source/um/UM.h"
#include "../../Framework/source/db/Database.h"
#include "./WatchListData.h"
#include "PreviousDayValues.h"
#include "WrapperWeb.h"
#include "../../Ssl/source/Ssl.h"

#define var const auto
#ifndef _MSC_VER
namespace Jde{  string OSApp::CompanyName()noexcept{ return "Jde-Cpp"; } }
#endif
namespace Jde::Markets::TwsWebSocket
{
	α Startup( bool initialCall=true )noexcept->void;
}

α main( int argc, char** argv )->int
{
	using namespace Jde;
	int result = 1;
	try
	{
		OSApp::Startup( argc, argv, "TwsWebSocket", "Web socket service for Tws Gateway" );
		IApplication::AddThread( std::make_shared<Threading::InterruptibleThread>("Startup", [&](){Markets::TwsWebSocket::Startup();}) );

		IApplication::Pause();
		IApplication::Cleanup();
		result = 0;
	}
	catch( const IException& e )
	{
		auto& os = e.Level()==ELogLevel::Trace ? std::cout : std::cerr;
		os << e.what() << std::endl;
	}
	return result;
}

namespace Jde::Markets
{
	α TwsWebSocket::Startup( bool initialCall )noexcept->void
	{
		//if( Settings::TryGet<bool>("um/use").value_or(false) ) currently need to configure um so meta is loaded.
		{
			try
			{
				UM::Configure();
			}
			catch( const IException& e )
			{
				e.Log();
				CRITICAL( "Could not configure user tables. - {}"sv, e.what() );
				std::cerr << e.what() << std::endl;
				std::this_thread::sleep_for( 1s );
				std::terminate();
			}
		}
		sp<TwsClientSync> pClient;
		sp<WrapperWeb> pWrapper;
		TRY( auto p = TwsWebSocket::WrapperWeb::CreateInstance(); pClient = get<0>( p ); pWrapper = get<1>( p );  );
		auto pSocket = TwsWebSocket::WebCoSocket::Create( pClient, Settings::Get<uint16>("twsWebSocket/port").value_or(6812), Settings::Get<uint8>("twsWebSocket/threadCount").value_or(1) );
		if( pWrapper )
		{
			pWrapper->SetWebSend( pSocket->WebSend() );
			if( initialCall )
			{
				Threading::SetThreadDscrptn( "Startup" );
				while( !Threading::GetThreadInterruptFlag().IsSet() && !TwsClientSync::IsConnected() )
					std::this_thread::yield();
			}
		}
		if( Settings::Get<bool>("twsWebSocket/loadOnStartup").value_or(false) )
		{
			try
			{
				var symbol = ""sv;//"AAPL"sv;
				var pPositions = TwsClientSync::Instance().RequestPositions().get();
				for( var& position : *pPositions )
				{
					if( !symbol.size() || position.contract().symbol()==symbol )
						Try( [&](){PreviousDayValues( position.contract().id() );} );
				}
				if( !symbol.size() )
				{
					try
					{
						for( var& name : WatchListData::Names() )
						{
							var pContent = WatchListData::Content( name );
							for( auto i=0; i<pContent->securities_size(); ++i )
								if( var id=pContent->securities(i).contract_id(); id )
									Try( [&](){PreviousDayValues( id );} );
						}
					}
					catch( const IOException& )
					{}
				}
			}
			catch( const IBException& )
			{}
		}
		if( initialCall )
		{
			DBG( "Startup Loading Complete."sv );
			IApplication::RemoveThread( "Startup" )->Detach();
		}
	}
}