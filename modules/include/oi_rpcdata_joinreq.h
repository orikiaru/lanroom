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
    
    explicit JoinReq (){}
    JoinReq ( const std::string & l_sRoom, const std::string & l_sMac, const RoomAttr & l_stAttr )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, stAttr);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 3> fields = {{
            OI_PB_FIELD(JoinReq, 0, sRoom),
            OI_PB_FIELD(JoinReq, 1, sMac),
            OI_PB_FIELD(JoinReq, 2, stAttr),
        }};
        std::array<uint8_t, 3> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
