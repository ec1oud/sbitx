# sBitx / zBitx - K7IHZ / LB2JK version
![sBitx image](sbitx44.png)

An improved experimental version of the sBitx application designed for the sBitx and zBitx hardware. 
https://www.hfsignals.com/

A 64-bit Raspberry Pi image can be downloaded [here](https://github.com/drexjj/sbitx/releases).
But in fact, this version works on either the 32-bit or 64-bit OS, on either sBitx or zBitx.

I'm mainly using FT8 and other digital modes. I've not learned Morse Code yet, and the
software decoder doesn't seem to perform very well, so CW mode is not tested much in this fork.

Near-term goals:
- [x] Fix bugs and make the GTK UI a little more useable
- [x] Make FT8 handle non-standard callsigns etc.
- [x] Replace the ad-hoc colored-text markup with plain text and separate spans
- [ ] Make the zBitx work well enough to do SOTA activations and take on trips
- [ ] Use 9p protocol to expose the radio as a virtual filesystem (see the 9p branch, for now)
- [ ] Turn the application into a headless daemon (GTK optional)
- [ ] Develop 9p client applications: one on Plan 9 and one using Qt Quick
- [ ] Use 9p over i2c for the zBitx display
- [ ] FT4?
- [ ] integrate with Pat? (winlink replacement, which already has good front/backend separation)
- [ ] Make other modes besides FT8/FT4 work better, somehow

The zBitx display is rather small, so I feel the need to use it mostly via some
sort of remote control; and anyway, the only built-in digital mode that the UI
supports directly so far is FT8, although it seems there was some vision to
eventually integrate with fldigi somehow, for the others. I hope that if the
first part of this project is successful, it will help to follow the same
pattern with other UIs: write a daemon to run whatever mode, and use 9p as the
remote-UI protocol. That way it will work over the network too, with minimal
bandwidth. Then maybe in the portable cases I can still mostly use a tablet
or laptop connected over wifi, rather than the tiny touchscreen.

It already had a web UI, still does...but I don't think I'll work on it much.

The FT8/FT4 improvements depend on improving https://github.com/ec1oud/ft8_lib/
at the same time. That's going on in parallel, in my fork.

