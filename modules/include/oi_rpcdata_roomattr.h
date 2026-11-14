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
        static constexpr std::array<::OI::Base::PbField, 2> fields = {{
            OI_PB_FIELD(RoomAttr, 0, iTcpRelay),
            OI_PB_FIELD(RoomAttr, 1, iVersion),
        }};
        std::array<uint8_t, 2> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
