#include "./TypeDefs.h"

namespace Jde::Markets::TwsWebSocket{ struct ProcessArg;}
namespace Jde::Markets::TwsWebSocket::News
{
	void RequestArticle( const string& providerCode, const string& articleId, const ProcessArg& arg )noexcept;
	void RequestHistorical( ContractPK contractId, const google::protobuf::RepeatedPtrField<string>& providerCodes, uint limit, time_t start, time_t end, const ProcessArg& arg )noexcept;
}
