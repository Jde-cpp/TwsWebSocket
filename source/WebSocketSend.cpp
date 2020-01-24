#include "WebSocket.h"
#include "EWebSend.h"
#include "../../MarketLibrary/source/TwsClient.h"


#define var const auto

using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

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

	void WebSocket::AddError( TickerId id, int errorCode, const std::string& errorString )noexcept
	{
		var [sessionId,clientId] = GetClientRequest( id );
		if( sessionId )
			AddError( sessionId, clientId, errorCode, errorString );
		else if( id>0 )
			DBG( "Could not find session for error req:  '{}'.", id );
	}
	void WebSocket::AddError( SessionId sessionId, ClientRequestId clientId, int errorCode, const std::string& errorString )noexcept
	{
		auto pError = new Proto::Results::Error(); pError->set_requestid(clientId); pError->set_code(errorCode); pError->set_message(errorString);
		auto pMessage = make_shared<Proto::Results::MessageUnion>(); pMessage->set_allocated_error( pError );
		AddOutgoing( sessionId, pMessage );
	}

	void WebSocket::AddOutgoing( SessionId id, MessageTypePtr pUnion )noexcept
	{
		function<void(Queue<MessageType>&)> afterInsert = [&pUnion]( Queue<MessageType>& queue ){ queue.Push( pUnion ); };
		_outgoing.Insert( afterInsert, id, sp<Queue<MessageType>>{new Queue<MessageType>()} );
	}
	void WebSocket::AddOutgoing( SessionId id, const vector<MessageTypePtr>& outgoing )noexcept
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
			DBG( "Could not find session for messageId:  '{}' req:  '{}'.", messageId, ibReqId );
			return;
		}

		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_int_value( clientId ); pMessage->set_type( messageId );
		auto pUnion = make_shared<Proto::Results::MessageUnion>(); pUnion->set_allocated_message( pMessage );
		AddOutgoing( sessionId, pUnion );
	}

	void WebSocket::Push( const Proto::Results::Position& position )noexcept
	{
		function<void(const Collections::UnorderedSet<SessionId>&)> addMessage = [&]( const Collections::UnorderedSet<SessionId>& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId )
				{
					auto pMessageUnion = make_shared<MessageType>(); pMessageUnion->set_allocated_position( new Proto::Results::Position{position} );
					AddOutgoing( sessionId, pMessageUnion );
				});
		};
		_requestSessions.Where( Proto::Results::EResults::PositionData, addMessage );
	}
	void WebSocket::Push( Proto::Results::EResults type, function<void(MessageType&)> set )noexcept
	{
		_requestSessions.Where( type, [&]( const auto& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId )
			{
				auto pMessageUnion = make_shared<MessageType>();
				set( *pMessageUnion );
				AddOutgoing( sessionId, pMessageUnion );
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
			AddOutgoing( sessionId, pUnion );
		}
		else
			DBG( "request {} not found", id );
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
			DBG( "Could not find session for req:  '{}'.", reqId );
	}
	void WebSocket::PushAllocated( TickerId reqId, Proto::Results::ContractDetails* pDetails )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( sessionId )
		{
			pDetails->set_requestid( clientReqId );
			auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_contract_details( pDetails );
			AddOutgoing( sessionId, pMessageUnion );
		}
		else
			DBG( "Could not find session for req:  '{}'.", reqId );
	}
	void WebSocket::ContractDetailsEnd( ReqId reqId )
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( !sessionId )
		{
			DBG( "Could not find session for ContractDetailsEnd req:  '{}'.", reqId );
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
			AddOutgoing( sessionId, pComplete );
		else if( !multi )
			Push( reqId, Proto::Results::EResults::ContractDataEnd );
	}
	void WebSocket::PushAllocated( TickerId reqId, Proto::Results::HistoricalData* pMessage )noexcept
	{
		var [sessionId, clientReqId] = _requestSession.Find( reqId, make_tuple(0,0) );
		if( sessionId )
		{
			pMessage->set_requestid( clientReqId );
			auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_historical_data( pMessage );
			AddOutgoing( sessionId, pMessageUnion );
		}
		else
			DBG( "Could not find session for req:  '{}'.", reqId );
	}
	void WebSocket::PushAllocated( SessionId sessionId, Proto::Results::MessageValue* pMessage )noexcept
	{
		auto pMessageUnion = make_shared<MessageType>();  pMessageUnion->set_allocated_message( pMessage );
		AddOutgoing( sessionId, pMessageUnion );
	}
/*	void WebSocket::Push( Proto::Results::EResults webSend, sp<MessageType> pMessageUnion )noexcept
	{
		function<void(const Collections::UnorderedSet<SessionId>&)> addMessage = [&]( const Collections::UnorderedSet<SessionId>& sessionIds )
		{
			sessionIds.ForEach( [&](const SessionId& sessionId ){ AddOutgoing( sessionId, pMessageUnion ); });
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
		var requestId = pMessage->requestid();
		var [sessionId, clientReqId] = _requestSession.Find( requestId, make_tuple(0,0) );
		if( sessionId )
		{
			pMessage->set_requestid( clientReqId );
			auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update_multi( pMessage );
			AddOutgoing( sessionId, pUnion );
		}
		else
		{
			delete pMessage;
			DBG( "request {} not found", requestId );
			TwsClient::Instance().cancelPositionsMulti( requestId );
		}
	}

	void WebSocket::Push( Proto::Results::AccountUpdate& accountUpdate )noexcept
	{
		var accountNumber = accountUpdate.account();
		std::shared_lock<std::shared_mutex> l( _accountRequestMutex );
		var pAccountNumberSessions = _accountRequests.find( accountNumber );
		if( pAccountNumberSessions==_accountRequests.end() )
		{
			WARN( "No listeners for account update '{}'", accountNumber );
			TwsClient::Instance().reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_account_update( new Proto::Results::AccountUpdate{accountUpdate} );
				AddOutgoing( sessionId, pUnion );
			}
		}
	}
	void WebSocket::Push( Proto::Results::PortfolioUpdate& porfolioUpdate )noexcept
	{
		var accountNumber = porfolioUpdate.accountnumber();
		std::shared_lock<std::shared_mutex> l( _accountRequestMutex );
		var pAccountNumberSessions = _accountRequests.find( accountNumber );
		if( pAccountNumberSessions==_accountRequests.end() )
		{
			WARN( "No listeners for account update '{}'", accountNumber );
			TwsClient::Instance().reqAccountUpdates( false, accountNumber );
		}
		else
		{
			for( var sessionId : pAccountNumberSessions->second )
			{
				auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_portfolio_update( new Proto::Results::PortfolioUpdate{porfolioUpdate} );
				AddOutgoing( sessionId, pUnion );
			}
		}
	}
}