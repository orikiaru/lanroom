#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"

namespace OI { namespace RpcData {
class RoomCastReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::vector<RoomPeer> vPeers{};
    RoomAttr stAttr{};
    
    explicit RoomCastReq (){}
    RoomCastReq ( const std::string & l_sRoom, const std::vector<RoomPeer> & l_vPeers, const RoomAttr & l_stAttr )
    {
        sRoom = l_sRoom;
        vPeers = l_vPeers;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, vPeers);
        os.WritePb(2, stAttr);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 3> fields = {{
            OI_PB_FIELD(RoomCastReq, 0, sRoom),
            OI_PB_FIELD(RoomCastReq, 1, vPeers),
            OI_PB_FIELD(RoomCastReq, 2, stAttr),
        }};
        std::array<uint8_t, 3> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
