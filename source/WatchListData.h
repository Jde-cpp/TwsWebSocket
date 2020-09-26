
namespace Jde::Markets::Proto::Watch{ class File; }
namespace Jde::Markets::TwsWebSocket::WatchListData
{
	vector<string> Names( optional<bool> portfolio={} )noexcept(false);
	void SendLists( SessionId sessionId, ClientRequestId clientId, bool portfolio )noexcept;
	sp<Proto::Watch::File> Content( const string& watchName )noexcept(false);
	void SendList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
//	void CreateList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
	void Delete( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
	void Edit( SessionId sessionId, ClientRequestId clientId, const Proto::Watch::File& file )noexcept;
}