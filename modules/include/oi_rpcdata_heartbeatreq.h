#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class HeartbeatReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    uint8_t iSawPunch{};
    
    explicit HeartbeatReq (){}
    HeartbeatReq ( const std::string & l_sRoom, const std::string & l_sMac, const uint8_t & l_iSawPunch )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        iSawPunch = l_iSawPunch;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, iSawPunch);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 3> fields = {{
            OI_PB_FIELD(HeartbeatReq, 0, sRoom),
            OI_PB_FIELD(HeartbeatReq, 1, sMac),
            OI_PB_FIELD(HeartbeatReq, 2, iSawPunch),
        }};
        std::array<uint8_t, 3> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
