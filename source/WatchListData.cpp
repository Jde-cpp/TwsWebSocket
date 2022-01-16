#include "WatchListData.h"
//#include "WebSocket.h"
#include "../../Framework/source/io/ProtoUtilities.h"
#include <jde/Str.h>

#define var const auto
//#define _socket WebSocket::Instance()
namespace Jde::Markets::TwsWebSocket
{
	fs::path GetDir()noexcept(false)
	{
		var dir = Settings::Get<fs::path>( "twsWebSocket/watchDir" ).value_or( IApplication::Instance().ApplicationDataFolder()/"watches" ); THROW_IF( dir.empty(), "WatchDir not set" ); CHECK_PATH( dir, SRCE_CUR );
		return dir;
	}
	vector<string> WatchListData::Names( optional<bool> portfolio )noexcept(false)//IOException - watch list dir may not exist.
	{
		vector<string> names;
		var dir = GetDir();
		for( var& dirEntry : fs::directory_iterator(dir) )
		{
			if( !dirEntry.is_regular_file() || dirEntry.path().extension()!=".watch" )
				continue;
			var& path = dirEntry.path();
			try
			{
				var pFile = IO::Proto::Load<Proto::Watch::File>( path );
				if( !portfolio.has_value() || portfolio==pFile->is_portfolio() )
					names.push_back( pFile->name() );
			}
			catch( const IException& )
			{}
		}
		return names;
	}
	void WatchListData::SendLists( bool portfolio, const ProcessArg& inputArg )noexcept
	{
		std::thread( [arg=inputArg, portfolio]//todo remove thread.
		{
			try
			{
				auto pNames = new Proto::Results::StringList(); pNames->set_request_id( arg.ClientId );
				vector<string> names = Names( portfolio );
				for_each( names.begin(), names.end(), [pNames](var& name){ pNames->add_values(name);} );
				MessageType msg; msg.set_allocated_string_list( pNames );
				arg.Push( move(msg) );
			}
			catch( const IException& e )
			{
				arg.Push( "Could not load watch lists ", e );
			}
		}).detach();
	}

	up<Proto::Watch::File> WatchListData::Content( str name )noexcept(false)
	{
		THROW_IF( !name.size(), "did not specify a watch name value." );
		var dir = GetDir();
		var file = dir/Str::Replace( name+".watch", ' ', '_' );
		return IO::Proto::Load<Proto::Watch::File>( file );
	}

	void WatchListData::SendList( str watchName, const ProcessArg& inputArg )noexcept
	{
		std::thread( [arg=inputArg, name=watchName]
		{
			try
			{
				auto pFile = Content( name );
				var pWatchList = new Proto::Results::WatchList();
				pWatchList->set_request_id( arg.ClientId );
				pWatchList->set_allocated_file( pFile.release() );
				MessageType msg; msg.set_allocated_watch_list( pWatchList );
				arg.Push( move(msg) );
			}
			catch( IException& e )
			{
				arg.Push( "Could not load watch list", e );
			}
		}).detach();
	}
/*	void WatchListData::CreateList( const ClientKey& key, str watchName )noexcept
	{
		std::thread( [sessionId, clientId, name=watchName]()
		{
			try
			{
				var dir = GetDir();
				var path = dir/Str::Replace( name, ' ', '_' );
				if( fs::exists(path) )
					THROW( IOException(path, "already exists") );

				Proto::Watch::File file;
				file.set_name( name );
				IO::Proto::Save( file, path );
				arg.WebSendPtr->Push( sessionId, clientId, EResults::Accept );
			}
			catch( const Exception& e )
			{
				arg.WebSendPtr->PushError( sessionId, clientId, -1, e.what() );
			}
		}).detach();
	}*/
	void WatchListData::Delete( str watchName, const ProcessArg& inputArg )noexcept
	{
		std::thread( [arg=inputArg, name=watchName]
		{
			try
			{
				var dir = GetDir();
				var path = dir/Str::Replace( name, ' ', '_' ); CHECK_PATH( path, SRCE_CUR );
				fs::remove( path );
				arg.Push( EResults::Accept );
			}
			catch( IException& e )
			{
				arg.Push( "could not delete watchlist", e );
			}
		}).detach();
	}
	void WatchListData::Edit( const Proto::Watch::File& inputFile, const ProcessArg& inputArg )noexcept
	{
		std::thread( [arg=inputArg, file=inputFile]
		{
			try
			{
				var dir = GetDir();
				var name = file.name();
				THROW_IF( !name.size(), "need a watchlist name." );

				var path = dir/Str::Replace( name+".watch", ' ', '_' );
				IO::Proto::Save( file, path );
				arg.Push( EResults::Accept );
			}
			catch( IException& e )
			{
				arg.Push( "could not edit watchlist", e );
			}
		}).detach();
	}

}