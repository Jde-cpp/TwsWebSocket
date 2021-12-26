#include <jde/coroutine/Task.h>
#include "../WebRequestWorker.h"

namespace Jde::Twitter
{
	α Search( string symbol, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task;
	α Block( uint userId, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task;
}