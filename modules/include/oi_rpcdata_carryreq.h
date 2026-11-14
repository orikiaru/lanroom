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
        static constexpr std::array<::OI::Base::PbField, 4> fields = {{
            OI_PB_FIELD(CarryReq, 0, sRoom),
            OI_PB_FIELD(CarryReq, 1, sMac),
            OI_PB_FIELD(CarryReq, 2, iVirtualIp),
            OI_PB_FIELD(CarryReq, 3, sFrame),
        }};
        std::array<uint8_t, 4> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
