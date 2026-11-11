#!/bin/sh
# SPDX-License-Identifier: MIT
# Build and deploy the flasher to the server named in sshm (default DM32Mayhem). Needs sshm unlocked by fingerprint.
#   DM32_SERVER=DM32Mayhem ./deploy/deploy.sh
set -eu
SERVER=${DM32_SERVER:-DM32Mayhem}
cd "$(dirname "$0")/.."

web/tools/build.sh

sshm exec "$SERVER" 'set -e
id -u dm32mayhem >/dev/null 2>&1 || useradd --system --home /var/lib/dm32mayhem --shell /usr/sbin/nologin dm32mayhem
install -d -o dm32mayhem -g dm32mayhem -m 700 /var/lib/dm32mayhem
install -d -m 755 /opt/dm32mayhem
ufw allow 80/tcp >/dev/null
ufw allow 443/tcp >/dev/null'

sshm cp web/build/dm32mayhem-web "$SERVER:/opt/dm32mayhem/dm32mayhem-web.new"
sshm cp deploy/dm32mayhem.service "$SERVER:/etc/systemd/system/dm32mayhem.service"

sshm exec "$SERVER" 'set -e
chmod 755 /opt/dm32mayhem/dm32mayhem-web.new
mv -f /opt/dm32mayhem/dm32mayhem-web.new /opt/dm32mayhem/dm32mayhem-web
systemctl daemon-reload
systemctl enable dm32mayhem >/dev/null
systemctl restart dm32mayhem
sleep 2
systemctl --no-pager --full status dm32mayhem | head -20'

echo "deployed; certificate issuance may take a few seconds on the first start"
