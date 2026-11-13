#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"

namespace OI { namespace RpcData {
class JoinRes : public ::OI::Rpc::RpcData
{
public:
    uint32_t iVirtualIp{};
    std::vector<RoomPeer> vPeers{};
    RoomAttr stAttr{};
    
    explicit JoinRes (){}
    JoinRes ( const uint32_t & l_iVirtualIp, const std::vector<RoomPeer> & l_vPeers, const RoomAttr & l_stAttr )
    {
        iVirtualIp = l_iVirtualIp;
        vPeers = l_vPeers;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iVirtualIp);
        os.WritePb(1, vPeers);
        os.WritePb(2, stAttr);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        static constexpr std::array<::OI::Base::PbField, 3> fields = {{
            OI_PB_FIELD(JoinRes, 0, iVirtualIp),
            OI_PB_FIELD(JoinRes, 1, vPeers),
            OI_PB_FIELD(JoinRes, 2, stAttr),
        }};
        std::array<uint8_t, 3> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
