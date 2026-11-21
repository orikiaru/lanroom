# 管理面板 HTTPS 部署

用 nginx 做 443 终结，`manage` 的 8080 只绑定本机。

## 步骤

1. 域名 A 记录指向服务器公网 IP。
2. 云安全组放行 TCP 80、443，关闭对外 8080。
3. 让 manage 只监听本机（裸机部署设环境变量，Docker 部署已在 `compose.yml` 里绑了 `127.0.0.1`）：

   ```bash
   PORT=127.0.0.1:8080 ./lanroom-manage
   ```

4. 运行脚本，自动安装 nginx、申请 Let's Encrypt 证书并配置续期：

   ```bash
   sudo DOMAIN=panel.example.com EMAIL=you@example.com ./deploy/ssl/setup-ssl.sh
   ```

## 说明

- `/api/ws` WebSocket 已在 nginx 里配置升级头和长超时。
- nginx 会把 `X-Forwarded-For` 覆盖为真实客户端 IP，后端登录审计和 GeoIP 依赖它，不要改成追加。
- 17000/udp、17001/tcp 是 lanroom-server 的数据面，不经过 nginx，仍需直接对外开放。
- 证书由 certbot 的 systemd timer 自动续期，续期后会 reload nginx。
