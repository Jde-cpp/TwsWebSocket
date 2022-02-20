#include <jde/Str.h>
#include <jde/coroutine/Task.h>
#include <jde/markets/TypeDefs.h>
#include <jde/markets/types/Contract.h>
#include "../../../Framework/source/coroutine/Awaitable.h"

namespace Jde::Markets::TwsWebSocket{ struct ProcessArg;}
namespace Jde::Markets::TwsWebSocket::News
{
	using namespace Jde::Coroutine;
	α RequestArticle( str providerCode, str articleId, ProcessArg arg )noexcept->Task;

	α RequestProviders( ProcessArg arg )noexcept->Task;

	using THistoricalResult=Task;
	α  RequestHistorical( ContractPK contractId, google::protobuf::RepeatedPtrField<string> providerCodes, uint limit, time_t start, time_t end, ProcessArg arg )noexcept->THistoricalResult;

	using TGoogleResult=VectorPtr<sp<Proto::Results::GoogleNews>>; using TGoogleAsync=Task; using TGoogleCoResult=AsyncAwait;
	α Google( const CIString& symbol )noexcept->TGoogleCoResult;
}