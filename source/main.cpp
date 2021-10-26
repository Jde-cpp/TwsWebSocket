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
	shared_ptr<Settings::Container> SettingsPtr;
	void Startup( bool initialCall=true )noexcept;
}

int main( int argc, char** argv )
{
	using namespace Jde;
	int result = 1;//EXIT_SUCCESS;
	try
	{
		OSApp::Startup( argc, argv, "TwsWebSocket", "Web socket service for Tws Gateway" );
		IApplication::AddThread( std::make_shared<Threading::InterruptibleThread>("Startup", [&](){Markets::TwsWebSocket::Startup();}) );

		IApplication::Pause();
		IApplication::CleanUp();
		result = 0;
	}
	catch( const EnvironmentException& e )
	{
		std::cerr << e.what() << std::endl;
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
	void TwsWebSocket::Startup( bool initialCall )noexcept
	{
		//if( Settings::TryGet<bool>("um/use").value_or(false) ) currently need to configure um so meta is loaded.
		{
			try
			{
				UM::Configure();
			}
			catch( const IException& e )
			{
				CRITICAL( "Could not configure user tables. - {}"sv, e.what() );
				std::cerr << e.what() << std::endl;
				std::this_thread::sleep_for( 1s );
				std::terminate();
			}
		}
		sp<TwsClientSync> pClient;
		sp<WrapperWeb> pWrapper;
		TRY( auto p = TwsWebSocket::WrapperWeb::CreateInstance(); pClient = get<0>( p ); pWrapper = get<1>( p );  );
		auto pSettings = TwsWebSocket::SettingsPtr = Jde::Settings::Global().SubContainer( "twsWebSocket" );
		auto pSocket = TwsWebSocket::WebCoSocket::Create( *pSettings, pClient );
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
		if( pSettings->TryGet<bool>("loadOnStartup").value_or(false) )
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
