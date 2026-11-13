#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class PunchRes : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    
    explicit PunchRes (){}
    PunchRes ( const std::string & l_sMac )
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
            OI_PB_FIELD(PunchRes, 0, sMac),
        }};
        std::array<uint8_t, 1> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
