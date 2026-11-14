#pragma once
#include "oi_rpc.h"

namespace OI { namespace RpcData {
class RoomCastRes : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    
    explicit RoomCastRes (){}
    RoomCastRes ( const std::string & l_sRoom )
    {
        sRoom = l_sRoom;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 1> fields = {{
            OI_PB_FIELD(RoomCastRes, 0, sRoom),
        }};
        std::array<uint8_t, 1> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
