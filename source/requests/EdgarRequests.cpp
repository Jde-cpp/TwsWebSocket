#include "EdgarRequests.h"
#include "../../../Private/source/markets/edgar/Edgar.h"
#include "../../../Private/source/markets/edgar/Form13F.h"

namespace Jde::Markets::TwsWebSocket
{
	void EdgarRequests::Filings( Cik cik, ProcessArg&& inputArg )noexcept
	{
		std::thread( [cik, arg=move(inputArg)]()
		{
			try
			{
				auto p = Edgar::LoadFilings(cik); p->set_request_id( arg.ClientId );
				MessageType msg; msg.set_allocated_filings( p.release() );
				arg.Push( move(msg) );
			}
			catch( const Exception& e )
			{
				arg.Push( e );
			}
		}).detach();
	}

	void Investors2( function<up<Edgar::Proto::Investors>()> f, ProcessArg&& inputArg )noexcept
	{
		std::thread( [f, arg=move(inputArg)]()
		{
			try
			{
				auto p = f(); p->set_request_id( arg.ClientId );
				MessageType msg; msg.set_allocated_investors( p.release() );
				arg.Push( move(msg) );
			}
			catch( const Exception& e )
			{
				arg.Push( e );
			}
		}).detach();
	}

	void EdgarRequests::Investors( ContractPK contractId, ProcessArg&& arg )noexcept
	{
		return Investors2( [contractId]()
		{
			auto pDetails = TwsClientSync::ReqContractDetails( contractId ).get(); CHECK( (pDetails->size()==1) );
			return Edgar::Form13F::LoadInvestors( pDetails->front().longName );
		}, move(arg) );
	}
/*	void EdgarRequests::Investors( Cusip cusip, ProcessArg&& arg )noexcept
	{
		return Investors2( [&cusip](){ return Edgar::Form13F::LoadInvestors(cusip); }, move(arg) );
	}*/
}