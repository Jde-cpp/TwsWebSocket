#include "./TypeDefs.h"

namespace Jde::Markets::TwsWebSocket
{
	struct ProcessArg;
	void PreviousDayValues( ContractPK contractId )noexcept( false );
	void PreviousDayValues( const google::protobuf::RepeatedField<google::protobuf::int32>& contractIds, const ProcessArg& arg )noexcept;
}
