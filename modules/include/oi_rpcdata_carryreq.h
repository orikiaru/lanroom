#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class CarryReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    uint32_t iVirtualIp{};
    std::string sFrame{};
    
    explicit CarryReq (){}
    CarryReq ( const std::string & l_sRoom, const std::string & l_sMac, const uint32_t & l_iVirtualIp, const std::string & l_sFrame )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        iVirtualIp = l_iVirtualIp;
        sFrame = l_sFrame;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, iVirtualIp);
        os.WritePb(3, sFrame);
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
            case 0: os.ReadPb(field_size, sRoom); break;
            case 1: os.ReadPb(field_size, sMac); break;
            case 2: os.ReadPb(field_size, iVirtualIp); break;
            case 3: os.ReadPb(field_size, sFrame); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
