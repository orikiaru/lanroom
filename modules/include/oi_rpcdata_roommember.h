#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class RoomMember : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    uint32_t iSourceIp{};
    uint16_t iSourcePort{};
    uint32_t iVirtualIp{};
    uint8_t iDirect{};
    uint8_t iSawPunch{};
    int64_t iLastSeen{};
    
    explicit RoomMember (){}
    RoomMember ( const std::string & l_sMac, const uint32_t & l_iSourceIp, const uint16_t & l_iSourcePort, const uint32_t & l_iVirtualIp, const uint8_t & l_iDirect, const uint8_t & l_iSawPunch, const int64_t & l_iLastSeen )
    {
        sMac = l_sMac;
        iSourceIp = l_iSourceIp;
        iSourcePort = l_iSourcePort;
        iVirtualIp = l_iVirtualIp;
        iDirect = l_iDirect;
        iSawPunch = l_iSawPunch;
        iLastSeen = l_iLastSeen;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sMac);
        os.WritePb(1, iSourceIp);
        os.WritePb(2, iSourcePort);
        os.WritePb(3, iVirtualIp);
        os.WritePb(4, iDirect);
        os.WritePb(5, iSawPunch);
        os.WritePb(6, iLastSeen);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 7> fields = {{
            OI_PB_FIELD(RoomMember, 0, sMac),
            OI_PB_FIELD(RoomMember, 1, iSourceIp),
            OI_PB_FIELD(RoomMember, 2, iSourcePort),
            OI_PB_FIELD(RoomMember, 3, iVirtualIp),
            OI_PB_FIELD(RoomMember, 4, iDirect),
            OI_PB_FIELD(RoomMember, 5, iSawPunch),
            OI_PB_FIELD(RoomMember, 6, iLastSeen),
        }};
        std::array<uint8_t, 7> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
