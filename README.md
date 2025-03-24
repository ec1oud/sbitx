# sBitx - 9p version

![sBitx image](sbitx44.png)

An improved version of the sBitx application designed for the
[sBitx hardware](https://www.hfsignals.com/index.php/sbitx-v3/)
and hopefully later on, the zBitx.

This fork by K7IHZ / LB2JK is an experiment to turn the sbitx into a 9p virtual
file server. It listens on TCP port 1564. You can mount it on Linux with a 9p
client such as 9pfs:

  $ 9pfs sbitx.local -p 1564 /mnt/sbitx

or Plan 9:

  cpu> 9fs tcp!192.168.x.x!1564 /n/sbitx

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
There are other precedents on Linux too (especially for home automation), 
such as gpio (the old way!), [1-wire file system](https://owfs.org/),
and a [filesystem for X10 modules](https://wish.sourceforge.net/index1.html)
I have used all of these at some point (and recommend them). IMO it's a
much better pattern than inventing a new command protocol each time.

