# Lanroom 管理面板 HTTPS 反向代理。由 setup-ssl.sh 渲染 @DOMAIN@ / @UPSTREAM@ 后写入 /etc/nginx/conf.d/lanroom.conf

map $http_upgrade $lanroom_connection_upgrade {
    default upgrade;
    ''      close;
}

server {
    listen 80;
    listen [::]:80;
    server_name @DOMAIN@;

    # Let's Encrypt HTTP-01 校验与续期
    location /.well-known/acme-challenge/ {
        root /var/www/certbot;
    }

    location / {
        return 301 https://$host$request_uri;
    }
}

server {
    # 用 listen 参数写法，兼容 nginx 1.24（Ubuntu 24.04）；新版本会提示弃用但仍可用
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name @DOMAIN@;

    ssl_certificate     /etc/letsencrypt/live/@DOMAIN@/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/@DOMAIN@/privkey.pem;
    ssl_protocols       TLSv1.2 TLSv1.3;
    ssl_session_cache   shared:lanroom_ssl:10m;
    ssl_session_timeout 1d;
    ssl_session_tickets off;

    add_header Strict-Transport-Security "max-age=31536000" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header Referrer-Policy "strict-origin-when-cross-origin" always;

    client_max_body_size 2m;

    location / {
        proxy_pass http://@UPSTREAM@;
        proxy_http_version 1.1;

        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        # 覆盖而不是追加，避免客户端伪造 X-Forwarded-For（后端用它记录登录来源 IP）
        proxy_set_header X-Forwarded-For   $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;

        # /api/ws 实时通道
        proxy_set_header Upgrade    $http_upgrade;
        proxy_set_header Connection $lanroom_connection_upgrade;
        proxy_read_timeout  3600s;
        proxy_send_timeout  3600s;
        proxy_buffering     off;
    }
}
