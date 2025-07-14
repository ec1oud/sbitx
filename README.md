# zBitx systemd daemon
## Introduction
This is a daemon for controlling the zBitx transceiver. It is based on the official GTK client, from which all GTK GUI elements have been removed. This means that a graphical desktop environment is no longer required.
Control is still via the touchscreen of the zBitx frontend or via a browser.
The daemon is started and stopped with the support of systemd.

## Requirements

- Internet connection via WLAN
- Login as user “pi”, preferably via SSH. Alternatively, you can also use a directly connected monitor and keyboard.
- The old sbitx application has been terminated.

## Installation

The following describes the installation on the standard operating system with which the zBitx was delivered (32-bit Rasbian / Debian 10 Buster). Installation with more recent OS versions (Raspberry Pi OS, 64-bit) has not yet been tested.

1. Update
  ```
  sudo apt update
  sudo apt upgrade
  sudo systemctl reboot
  ```

2. Dependencies  
  ```
  sudo apt install libsystemd-dev
  ```

3. Download  
  ```
  cd
  git clone https://github.com/dg0jde/zbitxd.git
  ```

4. Build and install  
  ```
  ~/zbitxd/update
  ```
  This builds the zBitx daemon from the sources. The installation then takes place, creating a new system user “zbitxd”.
  In future, updates can also be installed with this command.

5. Copy the existing configurations  
  ```
  sudo cp ~/sbitx/data/hw_settings.ini /var/lib/zbitxd/
  sudo cp ~/sbitx/data/sbitx.db /var/lib/zbitxd/
  sudo cp ~/sbitx/data/user_settings.ini /var/lib/zbitxd/
  sudo chown zbitxd:zbitxd /var/lib/zbitxd/*
  ```

6. Start  
  ```
  sudo systemctl daemon-reload
  sudo systemctl start zbitxd
  ```
  zBitx should now work normally.

7. Automatic start  
  ```
  sudo systemctl enable zbitxd
  sudo raspi-config
  ```
  * 1 System Options  
    * S5 Boot / Auto Login  
      * B1 Console  

