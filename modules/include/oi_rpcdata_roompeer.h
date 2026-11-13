#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class RoomPeer : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    uint32_t iSourceIp{};
    uint16_t iSourcePort{};
    uint32_t iVirtualIp{};
    uint8_t iDirect{};
    
    explicit RoomPeer (){}
    RoomPeer ( const std::string & l_sMac, const uint32_t & l_iSourceIp, const uint16_t & l_iSourcePort, const uint32_t & l_iVirtualIp, const uint8_t & l_iDirect )
    {
        sMac = l_sMac;
        iSourceIp = l_iSourceIp;
        iSourcePort = l_iSourcePort;
        iVirtualIp = l_iVirtualIp;
        iDirect = l_iDirect;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sMac);
        os.WritePb(1, iSourceIp);
        os.WritePb(2, iSourcePort);
        os.WritePb(3, iVirtualIp);
        os.WritePb(4, iDirect);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 5> fields = {{
            OI_PB_FIELD(RoomPeer, 0, sMac),
            OI_PB_FIELD(RoomPeer, 1, iSourceIp),
            OI_PB_FIELD(RoomPeer, 2, iSourcePort),
            OI_PB_FIELD(RoomPeer, 3, iVirtualIp),
            OI_PB_FIELD(RoomPeer, 4, iDirect),
        }};
        std::array<uint8_t, 5> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
