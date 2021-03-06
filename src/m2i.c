/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>

#include "irc.h"
#include "mpd.h"
#include "preferences.h"

static void m2i_sighandler(gint sig);
static void m2i_open_signal_pipe(void);
static gboolean m2i_signal_parse(GIOChannel *source, GIOCondition condition,
		gpointer data);
static void m2i_cleanup(void);
static void m2i_fork(void);

static gint signal_pipe[2] = { -1, -1 };
static guint signal_source;
static GMainLoop *loop;

int main(int argc, char *argv[])
{
	struct sigaction sa;

	/* initialize the gobject typing system */
	g_type_init();

	/* parse config */
	parse_config();

	/* parse cli */
	parse_args(argc, argv);

	if (!prefs.foreground)
		m2i_fork();

	/* set up sighandler */
	m2i_open_signal_pipe();
	sa.sa_handler = m2i_sighandler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	/* connect to mpd */
	if (!mpd_connect())
		mpd_schedule_reconnect();

	/* connect to irc */
	irc_connect(NULL);

	/* set up events */
	loop = g_main_loop_new(NULL, FALSE);

	/* run main loop */
	g_main_loop_run(loop);

	/* clean up */
	m2i_cleanup();

	return 0;
}

static void m2i_sighandler(gint sig)
{
	if (write(signal_pipe[1], &sig, 1)) {
		/* TODO */
	}
}

static void m2i_cleanup(void)
{
	prefs_cleanup();
	irc_cleanup();
	mpd_cleanup();

	g_source_remove(signal_source);
}

static void m2i_open_signal_pipe(void)
{
	GIOChannel *channel;

	if (pipe(signal_pipe) < 0) {
		/* TODO */
	}
	channel = g_io_channel_unix_new(signal_pipe[0]);
	signal_source = g_io_add_watch(channel, G_IO_IN, m2i_signal_parse,
			NULL);
	g_io_channel_unref(channel);
}

static gboolean m2i_signal_parse(GIOChannel *source,
		G_GNUC_UNUSED GIOCondition condition,
		G_GNUC_UNUSED gpointer data)
{
	gint fd = g_io_channel_unix_get_fd(source);
	gushort sig;
	if (read(fd, &sig, 1) < 0) {
		/* TODO */
	} else {
		g_message("Caught signal %u, exiting.", sig);
		g_main_loop_quit(loop);
	}
	return TRUE;
}

static void m2i_fork(void)
{
	pid_t pid = fork();

	if (pid < 0) {
		g_critical("Failed to fork process.");
	} else if (pid > 0) { /* The parent */
		exit(EXIT_SUCCESS);
	}
}
