#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class LeaveReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    
    explicit LeaveReq (){}
    LeaveReq ( const std::string & l_sRoom, const std::string & l_sMac )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 2> fields = {{
            OI_PB_FIELD(LeaveReq, 0, sRoom),
            OI_PB_FIELD(LeaveReq, 1, sMac),
        }};
        std::array<uint8_t, 2> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
