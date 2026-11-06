#pragma once
#include "oi_header.h"
#include "rpc_enums.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_punchres.h"
#include "oi_rpcdata_frameres.h"
#include "oi_rpcdata_relayres.h"
#include "oi_rpcdata_proberes.h"
namespace OI { namespace Rpc {
template<typename T> struct RpcProtoId;
}}
namespace OI { namespace RpcData {
class JoinReq;
class JoinRes;
class HeartbeatReq;
class HeartbeatRes;
class PunchReq;
class PunchRes;
class FrameReq;
class FrameRes;
class RelayReq;
class RelayRes;
class ProbeReq;
class ProbeRes;
}}
namespace OI { namespace Rpc {
template<> struct RpcProtoId<::OI::RpcData::JoinReq> { using Response = ::OI::RpcData::JoinRes; static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::JoinRes> { static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatReq> { using Response = ::OI::RpcData::HeartbeatRes; static const proto_type_t iValue = 4102; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatRes> { static const proto_type_t iValue = 4102; };
template<> struct RpcProtoId<::OI::RpcData::PunchReq> { using Response = ::OI::RpcData::PunchRes; static const proto_type_t iValue = 4103; };
template<> struct RpcProtoId<::OI::RpcData::PunchRes> { static const proto_type_t iValue = 4103; };
template<> struct RpcProtoId<::OI::RpcData::FrameReq> { using Response = ::OI::RpcData::FrameRes; static const proto_type_t iValue = 4104; };
template<> struct RpcProtoId<::OI::RpcData::FrameRes> { static const proto_type_t iValue = 4104; };
template<> struct RpcProtoId<::OI::RpcData::RelayReq> { using Response = ::OI::RpcData::RelayRes; static const proto_type_t iValue = 4105; };
template<> struct RpcProtoId<::OI::RpcData::RelayRes> { static const proto_type_t iValue = 4105; };
template<> struct RpcProtoId<::OI::RpcData::ProbeReq> { using Response = ::OI::RpcData::ProbeRes; static const proto_type_t iValue = 4106; };
template<> struct RpcProtoId<::OI::RpcData::ProbeRes> { static const proto_type_t iValue = 4106; };
}}

