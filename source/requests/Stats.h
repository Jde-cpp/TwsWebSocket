#pragma once
#include <jde/coroutine/Task.h>
#include "../WebRequestWorker.h"

namespace Jde::Markets::TwsWebSocket::RequestStats
{
	α Handle( up<Proto::Requests::RequestStats> pRequest, ProcessArg client )ι->Coroutine::Task;
}