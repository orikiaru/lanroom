#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class ProbeRes : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    
    explicit ProbeRes (){}
    ProbeRes ( const std::string & l_sMac )
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
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, sMac); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
