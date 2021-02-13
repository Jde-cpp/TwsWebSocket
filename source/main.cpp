#include "WebCoSocket.h"
#include "Flex.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../Framework/source/um/UM.h"
#include "./WatchListData.h"
#include "PreviousDayValues.h"
#include "WrapperWeb.h"

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	shared_ptr<Settings::Container> SettingsPtr;
	void Startup( bool initialCall=true )noexcept;
}

int main( int argc, char** argv )
{
	int result = 0;//EXIT_SUCCESS;
	try
	{
		Jde::OSApp::Startup( argc, argv, "TwsWebSocket" );
		Jde::IApplication::AddThread( make_shared<Jde::Threading::InterruptibleThread>("Startup", [&](){Jde::Markets::TwsWebSocket::Startup();}) );

		Jde::IApplication::Pause();
		Jde::IApplication::CleanUp();
	}
	catch( const Jde::EnvironmentException& e )
	{
		std::cerr << e.what() << std::endl;
		CRITICAL0( string(e.what()) );
		result = 1;//EXIT_FAILURE
	}
	return result;
}

namespace Jde::Markets
{
	void TwsWebSocket::Startup( bool initialCall )noexcept
	{
		auto pSettings = TwsWebSocket::SettingsPtr = Jde::Settings::Global().SubContainer( "twsWebSocket" );
		auto pUserSettings = Jde::Settings::Global().SubContainer( "um" );
		if( pUserSettings && pUserSettings->Get2<bool>("use").value_or(false) )
		{
			try
			{
				UM::Configure();
			}
			catch( const Exception& e )
			{
				CRITICAL( "Could not configure user tables. - {}"sv, e.what() );
				std::this_thread::sleep_for( 1s );
				std::terminate();
			}
		}
		auto [pClient,pWrapper] = TwsWebSocket::WrapperWeb::CreateInstance();
		auto pSocket = TwsWebSocket::WebCoSocket::Create( *pSettings, pClient );
		pWrapper->SetWebSend( pSocket->WebSend() );
		if( initialCall )
		{
			Threading::SetThreadDscrptn( "Startup" );
			while( !Threading::GetThreadInterruptFlag().IsSet() && !TwsClientSync::IsConnected() )
				std::this_thread::yield();
		}
		return;
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
							if( var id=pContent->securities(i).contract_id(); id ) PreviousDayValues( id );
					}
				}
				catch( const IOException& e )
				{
					e.Log( "Could not load watch lists" );
				}
			}
		}
		catch( const IBException& e )
		{
			e.Log( "Startup Position load ending" );
		}

		if( initialCall )
		{
//			DBG0( "Call again to test cache."sv );
	//		Startup( false );
			DBG0( "Startup Loading Complete."sv );
		}
	}
}
