#!/bin/bash

# 启动 MariaDB
if [ ! -d "/var/lib/mysql/mysql" ]; then
    mysql_install_db --user=mysql --datadir=/var/lib/mysql
fi
mysqld_safe --user=mysql &
sleep 5

# 设置 root 密码并允许远程连接
mysql -e "ALTER USER 'root'@'localhost' IDENTIFIED BY 'root'; \
          CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED BY 'root'; \
          GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION; \
          FLUSH PRIVILEGES;"

# 启动 Redis（允许外部连接，修改配置文件）
redis-server /etc/redis/redis.conf --daemonize yes

# 前台运行 SSH（保持容器活跃）
/usr/sbin/sshd -D
