
#include "EWebReceive.h"
#include "WebSocket.h"
#include "WrapperWeb.h"

#include "../../MarketLibrary/source/TwsClient.h"
#include "../../MarketLibrary/source/types/Bar.h"
#include "../../MarketLibrary/source/types/Contract.h"
#include "../../MarketLibrary/source/types/Exchanges.h"
#include "../../MarketLibrary/source/types/MyOrder.h"
#include "../../MarketLibrary/source/types/proto/requests.pb.h"
#include "Flex.h"
#define var const auto

namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>
#define _client TwsClient::Instance()
namespace Jde::Markets::TwsWebSocket
{
	using Proto::Requests::ERequests;
	using Proto::Results::EResults;

	void WebSocket::DoSession( shared_ptr<Stream> pSession )noexcept
	{
		var sessionId = ++_sessionId;
		{
			Threading::SetThreadDescription( fmt::format("webReceive - {}", sessionId) );
			//IO::OStreamBuffer buffer( std::make_unique<std::vector<char>>(8192) );
			//td::ostream os( &buffer );
			try
			{
				pSession->accept();
				_sessions.emplace( sessionId, pSession );
			}
			catch( const boost::system::system_error& se )
			{
				if( se.code()==websocket::error::closed )
					DBG( "Socket Closed on session {} when sending acceptance.", sessionId );
				if( se.code() != websocket::error::closed )
					ERR( "Error sending app - {}. ", se.code().message() );
			}
			catch( const std::exception& e )
			{
				ERR0( e.what() );
			}
		}
		if( !_sessions.Find(sessionId) )
			return;
		auto pMessage = new Proto::Results::MessageValue(); pMessage->set_type( Proto::Results::EResults::Accept ); pMessage->set_intvalue( sessionId );
		PushAllocated( sessionId, pMessage );
		DBG( "Listening to session #{}", sessionId );
		UnPause();
		while( !Threading::GetThreadInterruptFlag().IsSet() )
		{
			try
			{
				boost::beast::multi_buffer buffers;
				//vector<char> baseBuffer{'\0','\0','\0','\0'};
				//auto buffer = boost::asio::dynamic_buffer( baseBuffer );
				//boost::beast::ostream(buffer) << "\0\0\0\0";//std::array<char,4>{'\0','\0','\0','\0'};
				//var str2 = boost::beast::buffers_to( buffer.data() );
				pSession->read( buffers );
				//vector<google::protobuf::uint8> data;
				//data.reserve( boost::asio::buffer_size(buffers.data()) );
				// auto pData = buffers.data();//if in for loop causes crash in release mode.
				// for( boost::asio::const_buffer buffer : boost::beast::detail::buffers_range(pData) )
				// 	data.insert( std::end(data), static_cast<const char*>(buffer.data()), static_cast<const char*>(buffer.data())+buffer.size() );  //static_cast<const google::protobuf::uint8*>(buffer.data()), buffer.size() );
				var data = boost::beast::buffers_to_string( buffers.data() );
				google::protobuf::io::CodedInputStream input( reinterpret_cast<const unsigned char*>(data.data()), (int)data.size() );
				Proto::Requests::RequestTransmission transmission;
				if( !transmission.MergePartialFromCodedStream(&input) )
					THROW( IOException("transmission.MergePartialFromCodedStream returned false") );
				for( var& message : transmission.messages() )
				{
					if( message.has_account_updates() )
						ReceiveAccountUpdates( sessionId, message.account_updates() );
					else if( message.has_account_updates_multi() )
						ReceiveAccountUpdatesMulti( sessionId, message.account_updates_multi() );
					else if( message.has_generic_requests() )
						ReceiveRequests( sessionId, message.generic_requests() );
					else if( message.has_mrkdatasmart() )
						ReceiveMarketDataSmart( sessionId, message.mrkdatasmart() );
					else if( message.has_contract_details() )
						ReceiveContractDetails( sessionId, message.contract_details() );
					else if( message.has_options() )
					{
						WARN0( "Need to implement options request" );
						AddError( sessionId, message.options().requestid(), 0, "Options are not implemented." );
					}
					//	ReceiveOptions( sessionId, message.options() );
					else if( message.has_historicaldata() )
						ReceiveHistoricalData( sessionId, message.historicaldata() );
					else if( message.has_flexexecutions() )
						ReceiveFlex( sessionId, message.flexexecutions() );
					else if( message.has_place_order() )
						ReceiveOrder( sessionId, message.place_order().id(), message.place_order().order(), message.place_order().contract() );
					else
						ERR( "Unknown Message '{}'", message.Value_case() );
				}
			}
			catch( const IOException& e )
			{
				ERR( "IOExeption returned: '{}'", e.what() );
			}
			catch(boost::system::system_error const& se)
			{
				auto code = se.code();
				if(  code == websocket::error::closed )
					DBG( "se.code()==websocket::error::closed, id={}", sessionId );
				else
				{
					if( code.value()==104 )//reset by peer
						DBG( "system_error returned: '{}' - closing connection - {}", se.code().message(), sessionId );
					else
						DBG( "system_error returned: '{}' - closing connection - {}", se.code().message(), sessionId );
					EraseSession( sessionId );
					break;
				}
			}
			catch( std::exception const& e )
			{
				ERR( "std::exception returned: '{}'", e.what() );
			}
			if( !_sessions.Find(sessionId) )
			{
				DBG( "Could not find session id {} exiting thread.", sessionId );
				break;
			}
		}
		DBG0( "Leaving WebSocket::DoSession" );
	}

	void WebSocket::AddRequestSessions( SessionId id, const vector<Proto::Results::EResults>& webSendMessages )noexcept
	{
		function<void(Collections::UnorderedSet<SessionId>&)> afterInsert = [id](Collections::UnorderedSet<SessionId>& values){values.emplace(id);};
		for( var sendMessage : webSendMessages )
			_requestSessions.Insert( afterInsert, sendMessage, shared_ptr<Collections::UnorderedSet<SessionId>>{new Collections::UnorderedSet<SessionId>{}} );
	}

	void WebSocket::ReceiveRequests( SessionId sessionId, const Proto::Requests::GenericRequests& request )noexcept
	{
		if( request.type()==ERequests::Positions )
		{
			AddRequestSessions( sessionId, {EResults::PositionEnd, EResults::PositionData} );
			if( _requests.emplace(ERequests::Positions) )
				_client.reqPositions();
		}
		else if( request.type()==ERequests::ManagedAccounts )
		{
			AddRequestSessions( sessionId, {EResults::ManagedAccounts} );
			_client.reqManagedAccts();
		}
		else if( request.type()==ERequests::CancelMarketData )
		{
			for( auto i=0; i<request.ids_size(); ++i )
			{
				auto ibId = FindRequestId( sessionId, request.ids(i) );
				if( ibId )
				{
					_client.cancelMktData( ibId );
					_requestSession.erase( ibId );
				}
				else
					WARN( "({})Could not find MktData clientID='{}'", sessionId, request.ids(i) );
			}
		}
		else
			WARN( "Unknown message '{}' received from '{}' - not forwarding to tws.", request.type(), sessionId );
	}
	void WebSocket::ReceiveHistoricalData( SessionId sessionId, const Proto::Requests::RequestHistoricalData& req )noexcept
	{
		var reqId = _client.RequestId();
		_requestSession.emplace( reqId, make_tuple(sessionId,req.requestid()) );
		var pIb = Jde::Markets::Contract{ req.contract() }.ToTws();

		const DateTime endTime{ Clock::from_time_t(req.date()) };
		const string endTimeString{ fmt::format("{}{:0>2}{:0>2} {:0>2}:{:0>2}:{:0>2} GMT", endTime.Year(), endTime.Month(), endTime.Day(), endTime.Hour(), endTime.Minute(), endTime.Second()) };
		const string durationString{ fmt::format("{} D", req.days()) };
		DBG( "reqHistoricalData( reqId='{}' sessionId='{}', contract='{}' )", reqId, sessionId, pIb->symbol );
		try
		{
			_client.reqHistoricalData( reqId, *pIb, endTimeString, durationString, BarSize::ToString((BarSize::Enum)req.barsize()), TwsDisplay::ToString((TwsDisplay::Enum)req.display()), req.userth() ? 1 : 0, 2/*formatDate*/, req.keepuptodate(), TagValueListSPtr{} );
		}
		catch( const Exception& e )//bar size, etc.
		{
			WebSocket::Instance().AddError( sessionId, reqId, e );
		}
	}
	void WebSocket::ReceiveFlex( SessionId sessionId, const Proto::Requests::FlexExecutions& req )noexcept
	{
		var date = Chrono::BeginningOfDay( Clock::from_time_t(req.date()) );
		var clientId = req.requestid();
		string accountNumber = req.accountnumber();
		std::thread( [sessionId, clientId, date, accountNumber]()
		{
			Flex::SendTrades( sessionId, clientId, accountNumber, date );
		}).detach();
	}

	void WebSocket::ReceiveOrder( SessionId sessionId, ClientRequestId clientId, const Proto::Order& order, const Proto::Contract& contract )noexcept
	{
		var reqId = _client.RequestId();
		_requestSession.emplace( reqId, make_tuple(sessionId,clientId) );
		var pIbContract = Jde::Markets::Contract{ contract }.ToTws();
		DBG( "({})receiveOrder( '{}', contract='{}' {}x{} )", reqId, sessionId, pIbContract->symbol, order.limit(), order.quantity() );
		_client.placeOrder( *pIbContract, Jde::Markets::MyOrder{reqId, order} );
	}
/*
	void WebSocket::ReceiveOptions( SessionId sessionId, const Proto::Requests::RequestOptions& options )noexcept
	{
		std::thread( [sessionId,options]()
		{
			Threading::SetThreadDescription( "ReceiveOptions" );
			try
			{
				auto currentTradingDay = Clock::now();
				for( ; IsHoliday(currentTradingDay); currentTradingDay-=24h );
				var last = PreviousTradingDay( currentTradingDay );
				Proto::Results::OptionValues* pResults = nullptr;
				for( auto lastLast = PreviousTradingDay(last); !pResults && lastLast>last-7*24h; lastLast = PreviousTradingDay(lastLast) )
					pResults =  OptionData::LoadDiff( options.contractid(), options.iscall(), lastLast, last );
				if( pResults )
				{
					pResults->set_requestid( options.requestid() );
					auto pUnion = make_shared<MessageType>(); pUnion->set_allocated_options( pResults );
					WebSocket::Instance().AddOutgoing( sessionId, pUnion );
				}
				else
					WebSocket::Instance().AddError( sessionId, options.requestid(), -2, "No previous dates found" );

			}
			catch( const RuntimeException& e )
			{
				WebSocket::Instance().AddError( sessionId, options.requestid(), -1, e.what() );
			}
		} ).detach();
	}
	*/
	void WebSocket::ReceiveContractDetails( SessionId sessionId, const Proto::Requests::RequestContractDetails& request )noexcept
	{
		var clientRequestId = request.requestid();
		unordered_set<TickerId> requestIds;
		for( int i=0; i<request.contracts_size(); ++i )
			requestIds.emplace( _client.RequestId() );
		{
			unique_lock l{_multiRequestMutex};
			_multiRequests.emplace( clientRequestId, requestIds );
		}
		int i=0;
		for( var reqId : requestIds )
		{
			var pIb = Jde::Markets::Contract{ request.contracts(i++) }.ToTws();
			DBG( "reqContractDetails( reqId='{}' sessionId='{}', contract='{}' )", reqId, sessionId, pIb->symbol );
			_requestSession.emplace( reqId, make_tuple(sessionId,clientRequestId) );
			_client.reqContractDetails( reqId, *pIb );
		}
	}

	void WebSocket::ReceiveMarketDataSmart( SessionId sessionId, const Proto::Requests::RequestMrkDataSmart& request )noexcept
	{
		var reqId = _client.RequestId();
		ibapi::Contract contract; contract.conId = request.contractid(); contract.exchange = "SMART";
		var ticks = StringUtilities::AddCommas( request.ticklist() );
		DBG( "receiveMarketDataSmart( reqId='{}' sessionId='{}', contract='{}' )", reqId, sessionId, contract.conId );
		_requestSession.emplace( reqId, make_tuple(sessionId,request.requestid()) );
		_client.reqMktData( reqId, contract, ticks, request.snapshot(), false, TagValueListSPtr() );
	}
	void WebSocket::ReceiveAccountUpdates( SessionId sessionId, const Proto::Requests::RequestAccountUpdates& accountUpdates )noexcept
	{
		var& account = accountUpdates.accountnumber();
		var subscribe = accountUpdates.subscribe();
		std::unique_lock<std::shared_mutex> l{ _accountRequestMutex };
		if( subscribe )
		{
			DBG( "({}) - subscribe account '{}'", sessionId, account );
			auto [pInserted, inserted] = _accountRequests.emplace( account, unordered_set<SessionId>{} );
			pInserted->second.emplace( sessionId );
			if( inserted )
			{
				DBG( "Subscribe to account updates '{}'", account );
				_client.reqAccountUpdates( true, account );
			}
		}
		else
		{
			DBG( "({}) - unsubscribe from account '{}'", sessionId, account );
			auto pAccountSessionIds = _accountRequests.find( account );
			bool cancel = pAccountSessionIds==_accountRequests.end();
			if( !cancel )
			{
				pAccountSessionIds->second.erase( sessionId );
				cancel = pAccountSessionIds->second.size()==0;
			}
			if( cancel )
			{
				DBG( "Unsubscribe from account '{}'", account );
				_accountRequests.erase( account );
				_client.reqAccountUpdates( false, account );
			}
		}//return & killed connection
	}
	void WebSocket::ReceiveAccountUpdatesMulti( SessionId sessionId, const Proto::Requests::RequestAccountUpdatesMulti& accountUpdates )noexcept
	{
		var reqId =  _client.RequestId();
		var& account = accountUpdates.accountnumber();
		_requestSession.emplace( reqId, make_tuple(sessionId,accountUpdates.requestid()) );
		DBG( "reqAccountUpdatesMulti( reqId='{}' sessionId='{}', account='{}', clientId='{}' )", reqId, sessionId, account, accountUpdates.requestid() );
		_client.reqAccountUpdatesMulti( reqId, account, accountUpdates.modelcode(), accountUpdates.ledgerandnlv() );
	}
}