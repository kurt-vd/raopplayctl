# raopplayctl
## Problem description

I have an Apple Airport Express purely for its audio output.
It is connected as an output of an MPD server via a fifo output
and raop_play. Raop_play will play the audio from the fifo to the
Airport Express device.

But raop_play does not know when MPD is actually playing music, or
when MPD has paused. When MPD is silent, the raop_play will stream
silence to the Airport Express device.
In that situation, the Airport Express busy being silent,
and unavailable for other use.

## Solution
raopplayctl is (also) a server that listen on a datagram (unix) socket.
Any program can send commands to it. Raopplayctl will collect them
and forward those to a raop_play instance (see below).

Whenever a **play <FILE>** is sent, raopplayctl will start a raop_play
instance and command fowarding starts, including the start command.

Note that no caching of earlier commands takes place.

Whenever a **stop** is sent, raopplayctl will forward the command
*offdelay* (default 5) seconds later, and terminate raop_play.
The targeted Airport Express becomes available then.

## MPD integration
For MPD to work properly, I created an alternate output plugin
that supersedes the fifo output, with the start/stop commands.

