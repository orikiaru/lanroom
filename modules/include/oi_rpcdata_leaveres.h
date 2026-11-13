#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class LeaveRes : public ::OI::Rpc::RpcData
{
public:
    uint8_t iLeft{};
    
    explicit LeaveRes (){}
    LeaveRes ( const uint8_t & l_iLeft )
    {
        iLeft = l_iLeft;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iLeft);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 1> fields = {{
            OI_PB_FIELD(LeaveRes, 0, iLeft),
        }};
        std::array<uint8_t, 1> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
