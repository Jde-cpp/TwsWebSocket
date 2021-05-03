#include "../WebRequestWorker.h"

namespace Jde::Markets::TwsWebSocket::EdgarRequests
{
	using Cik = uint32;
	using Cusip=string;
	void Filings( Cik cik, ProcessArg&& inputArg )noexcept;
	void Investors( ContractPK contractPK, ProcessArg&& arg )noexcept;
	//void Investors( Cusip cusip, ProcessArg&& arg )noexcept;
}