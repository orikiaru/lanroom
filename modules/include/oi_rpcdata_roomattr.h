#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class RoomAttr : public ::OI::Rpc::RpcData
{
public:
    uint8_t iTcpRelay{};
    uint32_t iVersion{};
    
    explicit RoomAttr (){}
    RoomAttr ( const uint8_t & l_iTcpRelay, const uint32_t & l_iVersion )
    {
        iTcpRelay = l_iTcpRelay;
        iVersion = l_iVersion;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iTcpRelay);
        os.WritePb(1, iVersion);
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
            case 0: os.ReadPb(field_size, iTcpRelay); break;
            case 1: os.ReadPb(field_size, iVersion); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
