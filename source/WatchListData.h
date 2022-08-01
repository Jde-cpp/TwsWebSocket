
namespace Jde::Markets::Proto::Watch{ class File; }
namespace Jde::Markets::TwsWebSocket{ struct ProcessArg;}
namespace Jde::Markets::TwsWebSocket::WatchListData
{
	α Names( optional<bool> portfolio={} )ε->vector<string>;
	α SendLists( bool portfolio, const ProcessArg& inputArg )ι->void;
	α Content( str watchName )ε->up<Proto::Watch::File>;
	α SendList( str watchName, const ProcessArg& key )ι->void;
	α Delete( str watchName, const ProcessArg& key )ι->void;
	α Edit( Proto::Watch::File&& file, const ProcessArg& key )ι->Task;
}