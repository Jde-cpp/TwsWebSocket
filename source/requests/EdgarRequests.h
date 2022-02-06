#include "../WebRequestWorker.h"

namespace Jde::Markets::TwsWebSocket::EdgarRequests
{
	using Cik = uint32;
	using Cusip=string;
	α Filings( Cik cik, ProcessArg&& inputArg )noexcept->void;
	α Investors( ContractPK contractPK, ProcessArg arg )noexcept->Task;
}