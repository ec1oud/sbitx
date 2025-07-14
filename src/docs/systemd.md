# Running as systemd daemon
## Prepare

sudo apt install libsystemd-dev

sudo addgroup --system zbitx
sudo mkdir -m 02750 -p /var/lib/zbitx
sudo adduser --system --home /var/lib/zbitx --disabled-password zbitx --ingroup zbitx
sudo adduser zbitx gpio
sudo adduser zbitx audio
sudo chown zbitx:zbitx /var/lib/zbitx
