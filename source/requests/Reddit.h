//#include <jde/coroutine/Task.h>

namespace Jde::Reddit
{
	α Search( string&& symbol, string&& sort, up<Markets::TwsWebSocket::ProcessArg> arg )noexcept->Coroutine::Task2;
//	α Block( uint userId, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task2;
}