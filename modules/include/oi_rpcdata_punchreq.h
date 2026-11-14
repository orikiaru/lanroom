#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class PunchReq : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    
    explicit PunchReq (){}
    PunchReq ( const std::string & l_sMac )
    {
        sMac = l_sMac;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sMac);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 1> fields = {{
            OI_PB_FIELD(PunchReq, 0, sMac),
        }};
        std::array<uint8_t, 1> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
