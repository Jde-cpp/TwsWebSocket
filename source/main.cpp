#include "WebSocket.h"
#include "Flex.h"


#define var const auto
namespace Jde::Markets::TwsWebSocket
{
	shared_ptr<Settings::Container> SettingsPtr;
}

int main( int argc, char** argv )
{
	int result = EXIT_SUCCESS;
	try
	{
		Jde::OSApp::Startup( argc, argv, "TwsWebSocket" );

		Jde::Markets::TwsWebSocket::SettingsPtr = Jde::Settings::Global().SubContainer( "twsWebSocket" );
		var webSocketPort = Jde::Markets::TwsWebSocket::SettingsPtr->Get<Jde::uint16>( "webSocketPort" );

		//Jde::Markets::TwsWebSocket::Flex::SendTrades( 0, 0, "", Jde::DateTime(2019,10,25).GetTimePoint() );//get trades.
		//Jde::Markets::TwsWebSocket::Flex::SendTrades( 0, 0, "", Jde::DateTime(2019,10,25).GetTimePoint() );//get cache.

		Jde::Markets::TwsWebSocket::WebSocket::Create( webSocketPort );

		Jde::IApplication::Pause();
		Jde::IApplication::CleanUp();
	}
	catch( const Jde::EnvironmentException& e )
	{
		CRITICAL0( e.what() );
		result = EXIT_FAILURE;
	}
	return result;
}
