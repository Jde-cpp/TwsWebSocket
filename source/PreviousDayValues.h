#include "./TypeDefs.h"

namespace Jde::Markets::TwsWebSocket
{
	struct ProcessArg;
	α Previous( int32 contractId, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task2;
	void PreviousDayValues( ContractPK contractId )noexcept( false );
	void PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ProcessArg& arg )noexcept;
}
