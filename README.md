# s/zBitx systemd daemon
## Introduction
This is a daemon for controlling a sBitx or zBitx transceiver. It is based on a fork of the official GTK client, in which many bugs are fixed, and all GTK GUI elements have been removed. This means that a graphical desktop environment is no longer required.
Control is via the touchscreen of the zBitx, remote control, or via a browser. 9p support will be added back later, and a new client will be built for use on the sbitx touchscreen.
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

3. Download and check out branch
  ```
  cd
  git clone https://github.com/ec1oud/sbitx
  cd sbitx
  git checkout sbitxd
  ```

4. Build and install  
  ```
  make
  sudo make install
  ```
  This builds the sBitx daemon from the sources. The installation then takes place, creating a new system user “sbitxd”.
  In future, updates can also be installed the same way.

5. Copy the existing configurations  
  ```
  sudo cp ~/sbitx/data/hw_settings.ini /var/lib/sbitxd/
  sudo cp ~/sbitx/data/sbitx.db /var/lib/sbitxd/
  sudo cp ~/sbitx/data/user_settings.ini /var/lib/sbitxd/
  sudo chown sbitxd:sbitxd /var/lib/sbitxd/*
  ```

6. Start  
  ```
  sudo systemctl daemon-reload
  sudo systemctl start sbitxd
  ```
  The transceiver should now work normally.

7. Automatic start  
  ```
  sudo systemctl enable sbitxd
  sudo raspi-config
  ```
  * 1 System Options  
    * S5 Boot / Auto Login  
      * B1 Console  

