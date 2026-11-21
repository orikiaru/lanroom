#!/usr/bin/env bash
# 给 Lanroom 管理面板配置 nginx + Let's Encrypt HTTPS，8080 只监听本机不对外暴露。
# 适用 Debian/Ubuntu，需 root。可重复执行。
#
#   sudo DOMAIN=panel.example.com EMAIL=you@example.com ./deploy/ssl/setup-ssl.sh
#
# 前提：
#   1. DOMAIN 的 A 记录已指向本机公网 IP
#   2. 云厂商安全组放行 TCP 80 和 443（证书校验与访问都需要）
set -euo pipefail

DOMAIN="${DOMAIN:-}"
EMAIL="${EMAIL:-}"
UPSTREAM="${UPSTREAM:-127.0.0.1:8080}"
CONF=/etc/nginx/conf.d/lanroom.conf
WEBROOT=/var/www/certbot
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "$DOMAIN" ]]; then
  echo "请设置 DOMAIN，例如: sudo DOMAIN=panel.example.com $0" >&2
  exit 2
fi
if [[ "$(id -u)" -ne 0 ]]; then
  echo "需要 root 权限，请用 sudo 运行" >&2
  exit 1
fi

if ! getent hosts "$DOMAIN" >/dev/null; then
  echo "警告: 本机解析不到 $DOMAIN，请确认 DNS A 记录已生效，否则证书申请会失败" >&2
fi

echo "==> 安装 nginx 与 certbot"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq nginx certbot >/dev/null
mkdir -p "$WEBROOT"

render_full() {
  sed -e "s|@DOMAIN@|$DOMAIN|g" -e "s|@UPSTREAM@|$UPSTREAM|g" "$HERE/nginx-site.conf.tpl" > "$CONF"
}

CERT_DIR="/etc/letsencrypt/live/$DOMAIN"
if [[ ! -s "$CERT_DIR/fullchain.pem" ]]; then
  echo "==> 申请证书（先只开 80 端口做 HTTP-01 校验）"
  cat > "$CONF" <<EOF
server {
    listen 80;
    listen [::]:80;
    server_name $DOMAIN;
    location /.well-known/acme-challenge/ { root $WEBROOT; }
    location / { return 200 'lanroom ssl bootstrap'; add_header Content-Type text/plain; }
}
EOF
  nginx -t
  systemctl enable --now nginx
  systemctl reload nginx

  EMAIL_ARGS=(--register-unsafely-without-email)
  if [[ -n "$EMAIL" ]]; then
    EMAIL_ARGS=(--email "$EMAIL")
  fi
  certbot certonly --webroot -w "$WEBROOT" -d "$DOMAIN" \
    --non-interactive --agree-tos "${EMAIL_ARGS[@]}" \
    --deploy-hook "systemctl reload nginx"
else
  echo "==> 已存在证书，跳过申请"
fi

echo "==> 写入 HTTPS 反向代理配置"
render_full
nginx -t
systemctl enable --now nginx
systemctl reload nginx

# certbot 自带 systemd timer 自动续期；这里只做一次干跑验证（访问 Let's Encrypt 测试环境，网络差时可能很慢，限时 90 秒）
timeout 90 certbot renew --dry-run --quiet || echo "警告: 续期干跑失败或超时，不影响当前证书，可稍后手动执行 certbot renew --dry-run" >&2

echo
echo "完成: https://$DOMAIN  ->  http://$UPSTREAM"
echo "提醒: 请确保 manage 只监听 127.0.0.1（PORT=127.0.0.1:8080），并在安全组关闭对外的 8080。"
