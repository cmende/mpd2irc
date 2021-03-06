/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#ifndef HAVE_IRC_H
#define HAVE_IRC_H

gboolean irc_connect(G_GNUC_UNUSED gpointer data);
void irc_say(const gchar *msg, ...);
void irc_cleanup(void);

#endif /* HAVE_IRC_H */
