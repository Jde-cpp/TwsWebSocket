#include "./TypeDefs.h"

namespace Jde::Markets::TwsWebSocket
{
	struct ProcessArg;
	//α Previous( int32 contractId, Markets::TwsWebSocket::ProcessArg arg )noexcept->Coroutine::Task;
	α PreviousDayValues( ContractPK contractId )noexcept( false )->void;
	α PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ProcessArg& arg )noexcept->void;
}