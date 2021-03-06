mpd2irc
=======

mpd2irc is an interface between IRC and the [music player daemon](http://musicpd.org). It announces new songs on IRC (if enabled) and allows you to control your MPD via commands on IRC.


Dependencies
------------

* [glib/gio](http://gtk.org) (>= 2.22)
* [libmpdclient](http://musicpd.org) (>= 2.4)


Usage
-----

### Available commands ###

* `!announce`	enable/disable announcements
* `!next`	play next song
* `!np`		show currently playing song
* `!pause`	pause/resume playback
* `!play`	start playback
* `!prev`	play previous song
* `!random`	enable/disable random
* `!repeat`	enable/disable repeat
* `!status`	print mpd status
* `!stop`	stop playback
* `!version`	print version
