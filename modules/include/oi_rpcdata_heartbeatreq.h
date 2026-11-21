#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class HeartbeatReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    uint8_t iSawPunch{};
    std::string sToken{};
    
    explicit HeartbeatReq (){}
    HeartbeatReq ( const std::string & l_sRoom, const std::string & l_sMac, const uint8_t & l_iSawPunch, const std::string & l_sToken )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        iSawPunch = l_iSawPunch;
        sToken = l_sToken;
        
    }
    ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const override
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, iSawPunch);
        os.WritePb(3, sToken);
        return os;
    }
    ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os) override
    {
        static constexpr std::array<::OI::Base::PbField, 4> fields = {{
            OI_PB_FIELD(HeartbeatReq, 0, sRoom),
            OI_PB_FIELD(HeartbeatReq, 1, sMac),
            OI_PB_FIELD(HeartbeatReq, 2, iSawPunch),
            OI_PB_FIELD(HeartbeatReq, 3, sToken),
        }};
        std::array<uint8_t, 4> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
