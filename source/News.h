#include "./TypeDefs.h"

//namespace Jde::Markets::Proto::Watch{ class File; }
namespace Jde::Markets::TwsWebSocket::News
{
	//void RequestProviders( SessionId sessionId )noexcept;
	void RequestArticle( SessionId sessionId, ClientRequestId clientId, const string& providerCode, const string& articleId )noexcept;
	void RequestHistorical( SessionId sessionId, ClientRequestId clientId, ContractPK contractId, const google::protobuf::RepeatedPtrField<string>& providerCodes, uint limit, time_t start, time_t end )noexcept;
}
