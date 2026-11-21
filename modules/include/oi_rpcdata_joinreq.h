#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roomattr.h"

namespace OI { namespace RpcData {
class JoinReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    RoomAttr stAttr{};
    std::string sToken{};
    
    explicit JoinReq (){}
    JoinReq ( const std::string & l_sRoom, const std::string & l_sMac, const RoomAttr & l_stAttr, const std::string & l_sToken )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        stAttr = l_stAttr;
        sToken = l_sToken;
        
    }
    ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const override
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, stAttr);
        os.WritePb(3, sToken);
        return os;
    }
    ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os) override
    {
        static constexpr std::array<::OI::Base::PbField, 4> fields = {{
            OI_PB_FIELD(JoinReq, 0, sRoom),
            OI_PB_FIELD(JoinReq, 1, sMac),
            OI_PB_FIELD(JoinReq, 2, stAttr),
            OI_PB_FIELD(JoinReq, 3, sToken),
        }};
        std::array<uint8_t, 4> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
