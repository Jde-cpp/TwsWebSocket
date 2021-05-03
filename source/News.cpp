#include "News.h"
//#include "WebSocket.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "WebRequestWorker.h"
#include "../../Framework/source/io/ProtoUtilities.h"

//#define _client dynamic_cast<TwsClientCache&>(TwsClientSync::Instance())
#define _sync TwsClientSync::Instance()
//#define _socket WebSocket::Instance()

#define var const auto
namespace Jde::Markets::TwsWebSocket
{
/*	void News::RequestProviders( SessionId sessionId )noexcept
	{
		auto fnctn = [sessionId]()
		{
			try
			{
				var pProviders = _sync.RequestNewsProviders(sessionId).get();
				//Proto::Results::StringMap map;
				//for( var& [code,name] : providers )
				//	map.

				//_socket.Push( sessionId, EResults::NewsProviders, -1, e.what() );
				auto pMap = new Proto::Results::StringMap();
				pMap->set_result( EResults::NewsProviders );
				for( var& provider : *pProviders )
					(*pMap->mutable_values())[provider.providerCode] = provider.providerName;

				_socket.Push( EResults::NewsProviders, [&pMap]( auto& type ){ type.set_allocated_string_map( pMap ); } );
			}
			catch( const Exception& e )
			{
				_socket.PushError( sessionId, EResults::NewsProviders, -1, e.what() );
			}
		};
		std::thread( fnctn ).detach();
	}
*/
	void News::RequestArticle( str providerCode, str articleId, const ProcessArg& arg )noexcept
	{
		_sync.reqNewsArticle( arg.AddRequestSession(), providerCode, articleId );
	}
	void News::RequestHistorical( ContractPK contractId, const google::protobuf::RepeatedPtrField<string>& providerCodes, uint limit, time_t start, time_t end, const ProcessArg& arg )noexcept
	{
		_sync.reqHistoricalNews( arg.AddRequestSession(), contractId, IO::Proto::ToVector(providerCodes), limit, Clock::from_time_t(start), Clock::from_time_t(end) );
	}


}
