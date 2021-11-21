#pragma once

namespace Jde::Reddit
{
	α Search( string&& symbol, string&& sort, up<Markets::TwsWebSocket::ProcessArg> arg )noexcept->Coroutine::Task2;
	α Block( string&& user, up<Markets::TwsWebSocket::ProcessArg> arg )noexcept->Coroutine::Task2;
}