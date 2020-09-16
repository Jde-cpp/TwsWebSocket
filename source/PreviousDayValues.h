#include "./TypeDefs.h"

namespace Jde::Markets::TwsWebSocket
{
	void PreviousDayValues( ContractPK contractId )noexcept( false );
	void PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, SessionId sessionId, ClientRequestId requestId )noexcept;
}
