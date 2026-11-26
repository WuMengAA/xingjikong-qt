#!/bin/bash
# 云端部署收尾（一次性执行）
set -e
SRC=/home/admin/stelarith-cloud-src
echo "[1/4] 停掉 nohup 进程"
pkill -f 'node src/index.js' 2>/dev/null || true
sleep 1
echo "[2/4] 写 systemd unit"
cat > /etc/systemd/system/stelarith-cloud.service <<'UNIT'
[Unit]
Description=Stelarith Cloud
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=/home/admin/stelarith-cloud-src
EnvironmentFile=/home/admin/stelarith-cloud-src/.env
ExecStart=/usr/bin/node src/index.js
Restart=always
RestartSec=5
Environment=NODE_OPTIONS=--max-old-space-size=384

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable stelarith-cloud
systemctl start stelarith-cloud
sleep 2
echo "[3/4] 装 nginx 反代 80→8788（CF 回源用）"
if ! command -v nginx >/dev/null 2>&1; then
  DEBIAN_FRONTEND=noninteractive apt-get install -y nginx >/dev/null 2>&1 || echo "nginx 安装失败，跳过反代"
fi
if command -v nginx >/dev/null 2>&1; then
  cat > /etc/nginx/sites-available/stelarith-cloud <<'NGX'
server {
    listen 80 default_server;
    server_name _;
    location / {
        proxy_pass http://127.0.0.1:8788;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 3600s;
    }
}
NGX
  rm -f /etc/nginx/sites-enabled/default
  ln -sf /etc/nginx/sites-available/stelarith-cloud /etc/nginx/sites-enabled/stelarith-cloud
  nginx -t && systemctl restart nginx && echo "反代已启用"
fi
echo "[4/4] 健康检查"
systemctl is-active stelarith-cloud
curl -sS --max-time 5 http://127.0.0.1:8788/health
echo
curl -sS --max-time 5 http://127.0.0.1/health
echo
echo "SETUP_DONE"
