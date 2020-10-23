
namespace Jde::Markets::Proto::Watch{ class File; }
namespace Jde::Markets::TwsWebSocket{ struct ProcessArg;}
namespace Jde::Markets::TwsWebSocket::WatchListData
{
	vector<string> Names( optional<bool> portfolio={} )noexcept(false);
	void SendLists( bool portfolio, const ProcessArg& inputArg )noexcept;
	sp<Proto::Watch::File> Content( const string& watchName )noexcept(false);
	void SendList( const string& watchName, const ProcessArg& key )noexcept;
//	void CreateList( const string& watchName )noexcept;
	void Delete( const string& watchName, const ProcessArg& key )noexcept;
	void Edit( const Proto::Watch::File& file, const ProcessArg& key )noexcept;
}