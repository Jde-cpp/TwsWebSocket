
namespace Jde::Markets::Proto::Watch{ class File; }
namespace Jde::Markets::TwsWebSocket::WatchListData
{
	void SendLists( SessionId sessionId, ClientRequestId clientId, bool portfolio )noexcept;
	void SendList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
//	void CreateList( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
	void Delete( SessionId sessionId, ClientRequestId clientId, const string& watchName )noexcept;
	void Edit( SessionId sessionId, ClientRequestId clientId, const Proto::Watch::File& file )noexcept;
}