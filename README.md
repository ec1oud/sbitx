# s/zBitx systemd daemon

This is a daemon for controlling a sBitx or zBitx transceiver. It is
based on a fork of the official GTK client, in which many bugs are
fixed, and all GTK GUI elements have been removed. This means that a
graphical desktop environment is no longer required.  Control is via the
touchscreen of the zBitx, remote control, via a browser, or via a 9p
client application (there's a rough experimental one for Plan 9; another
for other operating systems will come later, intended for use on the
sbitx touchscreen, remote X11, phones and tablets etc.)

The daemon is started and stopped with the support of systemd. 
For now, it links with libsystemd; but to be a bit more system-agnostic,
maybe it shouldn't.

The 9p server listens on the standard TCP port 564. You can mount it on
Linux with a 9p userspace client such as 9pfs:

  $ 9pfs sbitx.local -t500 /mnt/sbitx

or the Linux kernel client implementation:

  $ sudo mount -t 9p sbitx.local -o trans=tcp,noextend,access=any /mnt/sbitx

or Plan 9:

  cpu> 9fs 192.168.x.x /n/sbitx

What you will see then is a hierarchy of virtual files: you can read all of
them, and write to some of them to set your callsign, grid, frequency etc.

Hopefully the sbitx will some day prove capable of handling multiple channels
at the same time, within its 25Khz passband; so the filesystem is laid out as
if it could already do that. If you want to use FT8 mode, for example, you are
concerned mainly with the files in `/mnt/sbitx/modes/ft8/1`.  You should be
able to `tail -f /mnt/sbitx/modes/ft8/1/received` to follow the incoming FT8
packets (but currently there's some trouble with that when using 9pfs on
Linux). I hope I can serve up real-time audio this way later: then maybe there
will be `/mnt/sbitx/modes/ssb/1/audio` for one channel, and you could
simultaneously monitor another voice channel, an FT8 channel, and so on.
Time will tell.

On Plan 9, this is more or less the usual pattern for supporting hardware.
There are other precedents on Linux for discovering hardware and
representing its features as virtual files (especially for home automation), 
such as gpio (the old way!), [1-wire file system](https://owfs.org/),
and a [filesystem for X10 modules](https://wish.sourceforge.net/index1.html)
I have used all of these at some point (and recommend them). IMO it's a
much better pattern than inventing a new command protocol each time.

## Requirements

- Internet connection via WLAN
- Login as user “pi”, preferably via SSH. Alternatively, you can also use a directly connected monitor and keyboard.
- The old sbitx application has been terminated.
- System time is maintained externally: sbitxd no longer does its own NTP queries nor reads the RTC.

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
  sudo apt install libsystemd-dev libixp-dev
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

8. Get hwclock working

  ```
  sudo echo 'dtoverlay=i2c-rtc-gpio,ds3231,i2c_gpio_sda=13,i2c_gpio_scl=6,addr=0x68' >> /boot/config.txt
  ```

  If you are outdoors or otherwise disconnected a lot (POTA SOTA etc.), you might want to install chrony instead of ntpd:
  it's better at maintaining time when the system has only intermittent Internet connectivity.  But the DS3231 hardware RTC is expected to be accurate anyway.

