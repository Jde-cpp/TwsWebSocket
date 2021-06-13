#include <jde/coroutine/Task.h>
#include <jde/markets/TypeDefs.h>
#include <jde/markets/types/Contract.h>
#include "../../../Framework/source/coroutine/Awaitable.h"

namespace Jde::Markets::TwsWebSocket{ struct ProcessArg;}
namespace Jde::Markets::TwsWebSocket::News
{
	using namespace Jde::Coroutine;
	α RequestArticle( str providerCode, str articleId, const ProcessArg& arg )noexcept->Task2;

	α RequestProviders( const ProcessArg& arg )noexcept->Task2;

	using THistoricalResult=Task2;//TaskError<up<Proto::Results::HistoricalNewsCollection>>;//TaskError<sp<Jde::Markets::Contract>>
	α  RequestHistorical( ContractPK contractId, const google::protobuf::RepeatedPtrField<string>& providerCodes, uint limit, time_t start, time_t end, const ProcessArg& arg )noexcept->THistoricalResult;

	using TGoogleResult=VectorPtr<sp<Proto::Results::GoogleNews>>; using TGoogleAsync=Task2; using TGoogleCoResult=ErrorAwaitableAsync;
	α Google( sv symbol )noexcept->TGoogleCoResult;

	TaskError<TGoogleResult> Google2( sv symbol )noexcept;
}
