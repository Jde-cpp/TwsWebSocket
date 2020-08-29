#include "WatchListData.h"
#include "WebSocket.h"
#include "../../Framework/source/io/ProtoUtilities.h"

#define var const auto
#define _socket WebSocket::Instance()
namespace Jde::Markets::TwsWebSocket
{
	fs::path GetDir()noexcept(false)
	{
		var dir = SettingsPtr->Get<fs::path>( "watchDir", fs::path{} );
		if( dir.empty() )
			THROW( EnvironmentException( "WatchDir not set") );
		IOException::TestExists( dir );
		return dir;
	}
	void WatchListData::SendLists( SessionId sessionId, ClientRequestId clientId, bool portfolio )noexcept
	{
		std::thread( [sessionId, clientId, portfolio]()
		{
			try
			{
				var dir = GetDir();
				vector<string> names;
				for( var& dirEntry : fs::directory_iterator(dir) )
				{
					if( !dirEntry.is_regular_file() || dirEntry.path().extension()!=".watch" )
						continue;
					var& path = dirEntry.path();
					try
					{
						var pFile = IO::ProtoUtilities::Load<Proto::Watch::File>( path );
						if( portfolio==pFile->is_portfolio() )
							names.push_back( pFile->name() );
					}
					catch( const Exception& e )
					{
						e.Log();
					}
				}
				auto pNames = new Proto::Results::StringList(); pNames->set_request_id( clientId );
				for_each( names.begin(), names.end(), [pNames](var& name){ pNames->add_values(name);} );
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_string_list( pNames );
				_socket.Push( sessionId, pUnion );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}
	void WatchListData::SendList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept
	{
		std::thread( [sessionId, clientId, name=watchName]()
		{
			try
			{
				if( !name.size() )
					THROW( Exception("did not specify a watch name value.") );
				var dir = GetDir();
				var file = dir/StringUtilities::Replace( name+".watch", ' ', '_' );
				var pFile = IO::ProtoUtilities::Load<Proto::Watch::File>( file, true );
				var pWatchList = new Proto::Results::WatchList();
				pWatchList->set_request_id( clientId );
				pWatchList->set_allocated_file( pFile.get() );
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_watch_list( pWatchList );
				_socket.Push( sessionId, pUnion );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}
/*	void WatchListData::CreateList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept
	{
		std::thread( [sessionId, clientId, name=watchName]()
		{
			try
			{
				var dir = GetDir();
				var path = dir/StringUtilities::Replace( name, ' ', '_' );
				if( fs::exists(path) )
					THROW( IOException(path, "already exists") );

				Proto::Watch::File file;
				file.set_name( name );
				IO::ProtoUtilities::Save( file, path );
				_socket.Push( sessionId, clientId, Proto::Results::EResults::Accept );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}*/
	void WatchListData::Delete( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept
	{
		std::thread( [sessionId, clientId, name=watchName]()
		{
			try
			{
				var dir = GetDir();
				var path = dir/StringUtilities::Replace( name, ' ', '_' );
				if( !fs::exists(path) )
					THROW( IOException(path, "watch does not exist") );

				fs::remove( path );
				_socket.Push( sessionId, clientId, Proto::Results::EResults::Accept );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}
	void WatchListData::Edit( SessionId sessionId, ClientRequestId clientId, const Proto::Watch::File& inputFile )noexcept
	{
		std::thread( [sessionId, clientId, file=inputFile]()
		{
			try
			{
				var dir = GetDir();
				var name = file.name();
				if( !name.size() )
					THROW( Exception("need a watchlist name.") );

				var path = dir/StringUtilities::Replace( name+".watch", ' ', '_' );
				IO::ProtoUtilities::Save( file, path );
				_socket.Push( sessionId, clientId, Proto::Results::EResults::Accept );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}

}
