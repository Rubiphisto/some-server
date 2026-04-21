#!/bin/bash
set -euo pipefail

DEV_USER="${DEV_USER:-dev}"
DEV_HOME="/home/${DEV_USER}"
AUTHORIZED_KEYS_FILE="${DEV_HOME}/.ssh/authorized_keys"

mkdir -p /var/run/sshd
mkdir -p /var/run/mysqld
mkdir -p /var/lib/redis
mkdir -p "${DEV_HOME}/.ssh"
mkdir -p "${DEV_HOME}/.codex"
mkdir -p /workspace

chown -R mysql:mysql /var/run/mysqld
chown -R redis:redis /var/lib/redis
chown -R "${DEV_USER}:${DEV_USER}" "${DEV_HOME}" /workspace
chmod 700 "${DEV_HOME}/.ssh"

# 允许通过环境变量注入密码
if [[ -n "${DEV_PASSWORD:-}" ]]; then
  echo "${DEV_USER}:${DEV_PASSWORD}" | chpasswd
fi

# 允许通过环境变量注入公钥（推荐）
if [[ -n "${DEV_AUTHORIZED_KEY:-}" ]]; then
  touch "${AUTHORIZED_KEYS_FILE}"
  grep -qxF "${DEV_AUTHORIZED_KEY}" "${AUTHORIZED_KEYS_FILE}" || echo "${DEV_AUTHORIZED_KEY}" >> "${AUTHORIZED_KEYS_FILE}"
fi

# 如果挂载了公钥文件，则复制进去
if [[ -f /run/dev_authorized_keys/authorized_keys ]]; then
  cp /run/dev_authorized_keys/authorized_keys "${AUTHORIZED_KEYS_FILE}"
fi

touch "${AUTHORIZED_KEYS_FILE}"
chown "${DEV_USER}:${DEV_USER}" "${AUTHORIZED_KEYS_FILE}"
chmod 600 "${AUTHORIZED_KEYS_FILE}"

echo "Starting MariaDB..."
service mariadb start

echo "Starting Redis..."
service redis-server start

echo "Starting SSH..."
exec /usr/sbin/sshd -D -e