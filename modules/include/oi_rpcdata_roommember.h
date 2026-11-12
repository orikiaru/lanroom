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
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, sMac); break;
            case 1: os.ReadPb(field_size, iSourceIp); break;
            case 2: os.ReadPb(field_size, iSourcePort); break;
            case 3: os.ReadPb(field_size, iVirtualIp); break;
            case 4: os.ReadPb(field_size, iDirect); break;
            case 5: os.ReadPb(field_size, iSawPunch); break;
            case 6: os.ReadPb(field_size, iLastSeen); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
