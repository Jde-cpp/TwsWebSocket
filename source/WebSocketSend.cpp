#include "WebSocket.h"
#include "EWebSend.h"
#include "../../MarketLibrary/source/client/TwsClientSync.h"
#include "../../Framework/source/Cache.h"

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

#define var const auto
#define _client TwsClientSync::Instance()

namespace Jde::Markets::TwsWebSocket
{
	void WebSocket::OnTimeout()noexcept
	{
		if( !_outgoing.size() )
			return;

		map<SessionId,vector<char>> buffers;
		std::function<void(const SessionId&, Queue<MessageType>&)> createBuffers = [&buffers]( const SessionId& id, Queue<MessageType>& queue )
		{
			if( !queue.size() )
				return;

			Proto::Results::Transmission transmission;
			std::function<void(MessageType&)> getFunction = [&transmission]( MessageType& message )
			{
				auto pNewMessage = transmission.add_messages();
				*pNewMessage = message;
			};
			queue.ForEach( getFunction );
			const size_t size = transmission.ByteSizeLong();
			auto& buffer = buffers.emplace( id, std::vector<char>(size) ).first->second;
			transmission.SerializeToArray( buffer.data(), (int)buffer.size() );
		};
		_outgoing.ForEach( createBuffers );
		_outgoing.eraseIf( [](const Queue<MessageType>& queue){ return queue.size()==0; } );
		set<SessionId> brokenIds;
		for( var& [id, buffer] : buffers )
		{
			auto pSession = _sessions.Find( id );
			if( !pSession )
			{
				_outgoing.erase( id );
				continue;
			}
			try
			{
				pSession->write( boost::asio::buffer(buffer.data(), buffer.size()) );
			}
			catch( boost::exception& e )
			{
				brokenIds.emplace( id ); ERRN( "removing because Error writing to Session:  {}", boost::diagnostic_information(&e) );
				try
				{
					pSession->close( websocket::close_code::none );
				}
				catch( const boost::exception& e2 )	{ERRN( "Error closing:  ", boost::diagnostic_information(&e2) );}
			}
		};
		for( var id : brokenIds )
			EraseSession( id );
	}

	void WebSocket::PushError( TickerId id, int errorCode, const std::string& errorString )noexcept
	{
		if( id>0 )
		{
			var [sessionId,clientId] = GetClientRequest( id );
			if( sessionId )
				PushError( sessionId, clientId, errorCode, errorString );
			else
				DBG( "Could not find session for error req:  '{}'."sv, id );
		}
		else if( errorCode==504 )
		{
			_requestSession.ForEach( [&](auto key, const auto& value)
			{
				PushError( get<0>(value), get<1>(value), errorCode, errorString );
			});
		}
	}
	void WebSocket::PushError( SessionId sessionId, ClientRequestId clientId, int errorCode, const std::string& errorString )noexcept
	{
		auto pError = new Proto::Results::Error(); pError->set_request_id(clientId); pError->set_code(errorCode); pError->set_message(errorString);
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_error( pError );
		Push( sessionId, pMessage );
	}

	void WebSocket::Push( SessionId id, MessageTypePtr pUnion )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [&pUnion]( Queue<MessageType>& queue ){ queue.Push( pUnion ); };
		_outgoing.Insert( afterInsert, id, sp<Queue<MessageType>>{new Queue<MessageType>()} );
	}

	void WebSocket::Push( SessionId id, const vector<MessageTypePtr>& outgoing )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [&outgoing]( Queue<MessageType>& queue )
		{
			for( var p : outgoing )
				queue.Push( p );
		};
		_outgoing.Insert( afterInsert, id, sp<Queue<MessageType>>{new Queue<MessageType>()} );
	}
	void WebSocket::Push( TickerId ibReqId, Proto::Results::EResults messageId )noexcept
	{
		var [sessionId,clientId] = GetClientRequest( ibReqId );
		if( !sessionId )
		{
			DBG( "Could not find session for messageId:  '{}' req:  '{}'."sv, messageId, ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_int_value( clientId ); pMessage->set_type( messageId );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_message( pMessage );
		Push( sessionId, pUnion );
	}

	void WebSocket::Push( const Proto::Results::Position& position )noexcept
	{
		function<void(const UnorderedSet<SessionId>&)> addMessage = [&]( const UnorderedSet<SessionId>& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId )
				{
					auto pMessageUnion = make_shared<MessageType>(); pMessageUnion->set_allocated_position( new Proto::Results::Position{position} );
					Push( sessionId, pMessageUnion );
				});
		};
		_requestSessions.Where( Proto::Results::EResults::PositionData, addMessage );
	}
	void WebSocket::Push( SessionId sessionId, ClientRequestId clientId, Proto::Results::EResults eResults )noexcept
	{
		auto pValue = new Proto::Results::MessageValue(); pValue->set_int_value( clientId ); pValue->set_type( Proto::Results::EResults::MultiEnd );
		WebSocket::PushAllocated( sessionId, pValue );
	}
	void WebSocket::Push( Proto::Results::EResults type, function<void(MessageType&)> set )noexcept
	{
		_requestSessions.Where( type, [&]( const auto& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId )
			{
				auto pMessageUnion = make_shared<MessageType>();
				set( *pMessageUnion );
				ASSERT( pMessageUnion->Value_case()!=MessageType::ValueCase::VALUE_NOT_SET );
				Push( sessionId, pMessageUnion );
			});
		});
	}
	bool WebSocket::PushAllocated( TickerId id, function<void(MessageType&, ClientRequestId)> set )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( id, make_tuple(0,0) );
		if( sessionId )
		{
			auto pUnion = make_shared<MessageType>();
			set( *pUnion, clientReqId );
			Push( sessionId, pUnion );
		}
		else
			DBG( "request {} not found"sv, id );
		return sessionId;
	}
	void WebSocket::PushAllocatedRequest( TickerId reqId, Proto::Results::MessageValue* pMessage )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( sessionId )
		{
			pMessage->set_int_value( clientReqId );
			PushAllocated( sessionId, pMessage );
		}
		else
			DBG( "Could not find session for req:  '{}'."sv, reqId );
	}
	void WebSocket::PushAllocated( TickerId reqId, Proto::Results::ContractDetails* pDetails )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( sessionId )
		{
			pDetails->set_request_id( clientReqId );
			auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_contract_details( pDetails );
			Push( sessionId, pMessageUnion );
		}
		else
			DBG( "Could not find session for req:  '{}'."sv, reqId );
	}
	void WebSocket::ContractDetailsEnd( ReqId reqId )
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( !sessionId )
		{
			DBG( "Could not find session for ContractDetailsEnd req:  '{}'."sv, reqId );
			return;
		}
		bool multi=false;
		sp<MessageType> pComplete;
		{
			unique_lock l{_multiRequestMutex};
			auto pMultiRequests = _multiRequests.find( clientReqId );
			multi = pMultiRequests!=_multiRequests.end();
			if( multi )
			{
				auto& reqIds = pMultiRequests->second;
				reqIds.erase( reqId );
				if( reqIds.size()==0 )
				{
					_multiRequests.erase( pMultiRequests );
					auto pMessage = new Proto::Results::MessageValue(); pMessage->set_type( Proto::Results::EResults::MultiEnd ); pMessage->set_int_value( clientReqId );
					pComplete = make_shared<MessageType>();  pComplete->set_allocated_message( pMessage );
				}
			}
		}
		if( pComplete )
			Push( sessionId, pComplete );
		else if( !multi )
			Push( reqId, Proto::Results::EResults::ContractDataEnd );
	}
	void WebSocket::PushAllocated( TickerId reqId, Proto::Results::HistoricalData* pMessage, bool saveCache )noexcept
	{
		if( saveCache )
		{
			std::lock_guard l{ _historicalCacheMutex };
			var crc = _historicalCrcs.Find( reqId, 0 );
			_historicalCrcs.erase( reqId );
			if( crc>0 )
				Cache::Set<Proto::Results::HistoricalData>( fmt::format("HistoricalData.{}",crc), make_shared<Proto::Results::HistoricalData>(*pMessage) );
		}
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( sessionId )
		{
			pMessage->set_request_id( clientReqId );
			auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_historical_data( pMessage );
			Push( sessionId, pMessageUnion );
		}
		else
			DBG( "Could not find session for req:  '{}'."sv, reqId );
	}
	void WebSocket::PushAllocated( SessionId sessionId, Proto::Results::MessageValue* pMessage )noexcept
	{
		auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_message( pMessage );
		Push( sessionId, pMessageUnion );
	}
/*	void WebSocket::Push( Proto::Results::EResults webSend, sp<MessageType> pMessageUnion )noexcept
	{
		function<void(const UnorderedSet<SessionId>&)> addMessage = [&]( const UnorderedSet<SessionId>& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId ){ Push( sessionId, pMessageUnion ); });
		};
		_requestSessions.Where( webSend, addMessage );
	}
	void WebSocket::PushAllocated( Proto::Results::AccountList* pAccounts )noexcept
	{
		auto pMessageUnion = make_shared<MessageType>();
		pMessageUnion->set_allocated_accountlist( pAccounts );
		Push( Proto::Results::EResults::ManagedAccounts, pMessageUnion );
	}*/
	void WebSocket::PushAllocated( Proto::Results::AccountUpdateMulti* pMessage )noexcept
	{
		var requestId = pMessage->request_id();
		var [sessionId, clientReqId] = _requestSession.Find( requestId, make_tuple(0,0) );
		if( sessionId )
		{
			pMessage->set_request_id( clientReqId );
			auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update_multi( pMessage );
			Push( sessionId, pUnion );
		}
		else
		{
			delete pMessage;
			DBG( "request {} not found"sv, requestId );
			_client.cancelPositionsMulti( requestId );
		}
	}
	void WebSocket::PushAllocated( TickerId requestId, Proto::Results::OptionParams* pMessage )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( requestId, make_tuple(0,0) );
		if( sessionId )
		{
			auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_option_parameters( pMessage );
			Push( sessionId, pUnion );
		}
		else
		{
			delete pMessage;
			DBG( "request {} not found"sv, requestId );
		}
	}
	void WebSocket::Push( Proto::Results::AccountUpdate& accountUpdate )noexcept
	{
		var accountNumber = accountUpdate.account();
		std::shared_lock<std::shared_mutex> l( _accountRequestMutex );
		var pAccountNumberSessions = _accountRequests.find( accountNumber );
		if( pAccountNumberSessions==_accountRequests.end() )
		{
			WARN( "No current listeners for account update '{}', reqAccountUpdates"sv, accountNumber );
			_client.reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update( new Proto::Results::AccountUpdate{accountUpdate} );
				Push( sessionId, pUnion );
			}
		}
	}
	void WebSocket::Push( Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept
	{
		var accountNumber = porfolioUpdate.account_number();
		std::shared_lock<std::shared_mutex> l( _accountRequestMutex );
		var pAccountNumberSessions = _accountRequests.find( accountNumber );
		if( pAccountNumberSessions==_accountRequests.end() )
		{
			WARN( "No listeners for portfolio update '{}'"sv, accountNumber );
			_client.reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate} );
				Push( sessionId, pUnion );
			}
		}
	}
}