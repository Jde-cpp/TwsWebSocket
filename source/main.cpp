#include "WebSocket.h"
#include "Flex.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "./WatchListData.h"
#include "PreviousDayValues.h"

#define var const auto

namespace Jde::Markets::TwsWebSocket
{
	shared_ptr<Settings::Container> SettingsPtr;
	void Startup()noexcept;
}

int main( int argc, char** argv )
{
	int result = 0;//EXIT_SUCCESS;
	try
	{
		Jde::OSApp::Startup( argc, argv, "TwsWebSocket" );

		Jde::Markets::TwsWebSocket::SettingsPtr = Jde::Settings::Global().SubContainer( "twsWebSocket" );
		var webSocketPort = Jde::Markets::TwsWebSocket::SettingsPtr->Get<Jde::uint16>( "webSocketPort" );

		Jde::Markets::TwsWebSocket::WebSocket::Create( webSocketPort );
		auto pStartup = make_shared<Jde::Threading::InterruptibleThread>( "LoadPositions", [&](){Jde::Markets::TwsWebSocket::Startup();} );
		Jde::IApplication::AddThread( pStartup );

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
	void TwsWebSocket::Startup()noexcept
	{
		while( !Threading::GetThreadInterruptFlag().IsSet() && !TwsClientSync::IsConnected() )
			std::this_thread::yield();
		var pPositions = TwsClientSync::Instance().RequestPositions().get();
		for( var& position : *pPositions )
			PreviousDayValues( position.contract().id() );
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
		static std::once_flag single;
		std::call_once( single, [&](){Startup();} );
	}
}
