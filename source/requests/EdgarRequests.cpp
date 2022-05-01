#include "EdgarRequests.h"
#include <jde/markets/types/proto/ResultsMessage.h>
#include <jde/markets/edgar/Edgar.h>
#include <jde/markets/edgar/types/Form13F.h>


#define var const auto
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
			catch( IException& e )
			{
				arg.Push( "could not retreive filings", e );
			}
		}).detach();
	}

	α EdgarRequests::Investors( ContractPK contractId, ProcessArg arg_ )noexcept->Task
	{
		var arg{ move(arg_) };
		try
		{
			auto pDetails = ( co_await Tws::ContractDetail(contractId) ).SP<::ContractDetails>();
			auto p = Edgar::LoadInvestors( pDetails->longName );//normally cached.
			arg.Push( ToMessage(arg.ClientId, p.release()) );
		}
		catch( IException& e )
		{
			arg.Push( "Could not load investors", e );
		}
	}
}