#!/bin/bash
set -e

echo "===> Starting services..."

# ========= SSH =========
echo "[+] Starting SSH..."
/usr/sbin/sshd

# ========= MariaDB =========
echo "[+] Initializing MariaDB..."

# 如果数据库未初始化，则初始化
if [ ! -d "/var/lib/mysql/mysql" ]; then
    echo "    -> First time setup"
    mysqld --initialize-insecure --user=mysql
fi

echo "[+] Starting MariaDB..."
mysqld_safe --user=mysql &

# 等待数据库启动
sleep 3

# ========= Redis =========
echo "[+] Starting Redis..."
redis-server --daemonize yes

# ========= 网络信息（调试用）=========
echo "[+] Network info:"
ip addr || true
cat /etc/resolv.conf || true

# ========= 保持容器运行 =========
echo "===> All services started"
tail -f /dev/null