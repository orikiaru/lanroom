#!/bin/sh
set -eu
cat > /tmp/lanroom-server.toml <<EOF
[ThreadPool]
threadcount = 1

[Timer]
offset_ms = 5
timer_lv_count = 4
timer_lvs = [128, 128, 128, 1]

[RedisClient]
ip = "${REDIS_HOST:-redis}"
port = "6379"
type = "tcp"
timeout_ms = 25000
max_pending = 128

[RoomServer]
ip = "0.0.0.0"
port = "17001"
type = "tcp"

[RoomUdp]
ip = "0.0.0.0"
port = "17000"
type = "udp"
idle_timeout_ms = 60000

[LanServer]
member_ttl = 30
auth_secret = "${AUTH_SECRET:-}"
EOF
exec /usr/local/bin/lanroom-server /tmp/lanroom-server.toml
