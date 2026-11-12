#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"

namespace OI { namespace RpcData {
class HeartbeatRes : public ::OI::Rpc::RpcData
{
public:
    uint32_t iVirtualIp{};
    std::vector<RoomPeer> vPeers{};
    RoomAttr stAttr{};
    
    explicit HeartbeatRes (){}
    HeartbeatRes ( const uint32_t & l_iVirtualIp, const std::vector<RoomPeer> & l_vPeers, const RoomAttr & l_stAttr )
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
        vPeers.clear();
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, iVirtualIp); break;
            case 1: os.ReadPb(field_size, vPeers); break;
            case 2: os.ReadPb(field_size, stAttr); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
};
}}
