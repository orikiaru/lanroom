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
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, iVirtualIp); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
