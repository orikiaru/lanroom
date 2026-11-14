#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class CarryRes : public ::OI::Rpc::RpcData
{
public:
    uint32_t iVirtualIp{};
    
    explicit CarryRes (){}
    CarryRes ( const uint32_t & l_iVirtualIp )
    {
        iVirtualIp = l_iVirtualIp;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iVirtualIp);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 1> fields = {{
            OI_PB_FIELD(CarryRes, 0, iVirtualIp),
        }};
        std::array<uint8_t, 1> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
