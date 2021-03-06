/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <glib.h>
#include <mpd/client.h>

#include "irc.h"
#include "mpd.h"
#include "preferences.h"

static gboolean mpd_parse(GIOChannel *channel, GIOCondition condition,
		gboolean user_data);
static void mpd_disconnect(void);
static gboolean mpd_reconnect(G_GNUC_UNUSED gpointer data);
static void mpd_update(void);
static void mpd_report_error(void);

static struct {
	struct mpd_connection *conn;
	struct mpd_status *status;
	struct mpd_song *song;
	guint idle_source;
	guint reconnect_source;
} mpd;

gboolean mpd_connect(void)
{
	mpd.conn = mpd_connection_new(prefs.mpd_server, prefs.mpd_port, 10000);
	mpd.idle_source = 0;

	if (mpd_connection_get_error(mpd.conn) != MPD_ERROR_SUCCESS) {
		g_warning("Failed to connect to MPD: %s",
				mpd_connection_get_error_message(mpd.conn));
		return FALSE;
	} else if (mpd_connection_cmp_server_version(mpd.conn, 0, 14, 0) < 0) {
		g_critical("MPD too old, please upgrade to 0.14 or newer");
		return FALSE;
	} else {
		GIOChannel *channel;

		mpd_command_list_begin(mpd.conn, TRUE);
		if (prefs.mpd_password)
			mpd_send_password(mpd.conn, prefs.mpd_password);
		mpd_send_status(mpd.conn);
		mpd_send_current_song(mpd.conn);
		mpd_command_list_end(mpd.conn);

		mpd.status = mpd_recv_status(mpd.conn);
		if (!mpd_response_next(mpd.conn)) {
			mpd_report_error();
			return FALSE;
		}
		mpd.song = mpd_recv_song(mpd.conn);
		if (!mpd_response_finish(mpd.conn)) {
			mpd_report_error();
			return FALSE;
		}

		g_message("Connected to MPD");
		irc_say("Connected to MPD");

		mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);

		channel = g_io_channel_unix_new(
				mpd_connection_get_fd(mpd.conn));
		mpd.idle_source = g_io_add_watch(channel, G_IO_IN,
				(GIOFunc) mpd_parse, NULL);
		g_io_channel_unref(channel);

		return TRUE;
	}
}

static gboolean mpd_parse(G_GNUC_UNUSED GIOChannel *channel,
		G_GNUC_UNUSED GIOCondition condition,
		G_GNUC_UNUSED gboolean user_data)
{
	mpd_recv_idle(mpd.conn, FALSE);

	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return FALSE;
	}

	mpd_update();

	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
	return TRUE;
}

static void mpd_disconnect(void)
{
	if (mpd.conn)
		mpd_connection_free(mpd.conn);
	mpd.conn = NULL;
	irc_say("Disconnected from MPD");
}

void mpd_schedule_reconnect(void)
{
	mpd.reconnect_source = g_timeout_add_seconds(30, mpd_reconnect, NULL);
}

static gboolean mpd_reconnect(G_GNUC_UNUSED gpointer data)
{
	if (!mpd_connect()) {
		mpd_disconnect();
		return TRUE; // try again
	}

	mpd.reconnect_source = 0;
	return FALSE; // remove event
}

static void mpd_update(void)
{
	enum mpd_state prev = MPD_STATE_UNKNOWN;

	if (mpd.status) {
		prev = mpd_status_get_state(mpd.status);
		mpd_status_free(mpd.status);
	}

	mpd.status = mpd_run_status(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}

	if (mpd_status_get_state(mpd.status) == MPD_STATE_PLAY &&
			prev != MPD_STATE_PAUSE) {
		if (mpd.song)
			mpd_song_free(mpd.song);
		mpd.song = mpd_run_current_song(mpd.conn);
		if (!mpd_response_finish(mpd.conn)) {
			mpd_report_error();
			return;
		}

		if (prefs.announce)
			mpd_announce_song();
	}
}

void mpd_announce_song(void)
{
	const gchar *artist, *song, *album;

	artist = mpd_song_get_tag(mpd.song, MPD_TAG_ARTIST, 0);
	song = mpd_song_get_tag(mpd.song, MPD_TAG_TITLE, 0);
	album = mpd_song_get_tag(mpd.song, MPD_TAG_ALBUM, 0);

	irc_say("Now playing: %s - %s (%s)", artist, song, album);
}

/* TODO: remove redundancy */
void mpd_next(void)
{
	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_next(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_say_status(void)
{
	gchar *state;
	gchar *artist, *title;

	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	if (mpd.status)
		mpd_status_free(mpd.status);

	mpd_run_noidle(mpd.conn);
	mpd.status = mpd_run_status(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);

	switch (mpd_status_get_state(mpd.status)) {
		case MPD_STATE_STOP:
			state = g_strdup("stopped"); break;
		case MPD_STATE_PLAY:
			state = g_strdup("playing"); break;
		case MPD_STATE_PAUSE:
			state = g_strdup("paused"); break;
		default:
			state = g_strdup("unknown"); break;
	}

	if (mpd.song) {
		artist = g_strdup(mpd_song_get_tag(mpd.song, MPD_TAG_ARTIST,
					0));
		title = g_strdup(mpd_song_get_tag(mpd.song, MPD_TAG_TITLE, 0));
	} else {
		artist = g_strdup("");
		title = g_strdup("");
	}

	irc_say("[%s] %s - %s (%i:%02i/%i:%02i) | repeat: %sabled | "
			"random: %sabled | announce: %sabled",
			state, artist, title,
			mpd_status_get_elapsed_time(mpd.status) / 60,
			mpd_status_get_elapsed_time(mpd.status) % 60,
			mpd_status_get_total_time(mpd.status) / 60,
			mpd_status_get_total_time(mpd.status) % 60,
			(mpd_status_get_repeat(mpd.status) ? "en" : "dis"),
			(mpd_status_get_random(mpd.status) ? "en" : "dis"),
			(prefs.announce ? "en" : "dis"));
	g_free(state);
	g_free(artist);
	g_free(title);
}

void mpd_cleanup(void)
{
	if (mpd.idle_source > 0)
		g_source_remove(mpd.idle_source);
	mpd_disconnect();
}

static void mpd_report_error(void)
{
	const gchar *error = mpd_connection_get_error_message(mpd.conn);
	g_warning("MPD error: %s", error);
	irc_say("MPD error: %s", error);
	if (mpd_connection_clear_error(mpd.conn)) {
		g_warning("Unable to recover, reconnecting");
		mpd_disconnect();
		mpd_schedule_reconnect();
	}
}

void mpd_play(void)
{
	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_play(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_pause(void)
{
	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_toggle_pause(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_prev(void)
{
	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_previous(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_repeat(void)
{
	const gboolean mode = !mpd_status_get_repeat(mpd.status);

	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_repeat(mpd.conn, mode);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	irc_say("Repeat %sabled", (mode ? "en" : "dis"));
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_random(void)
{
	const gboolean mode = !mpd_status_get_random(mpd.status);

	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_random(mpd.conn, mode);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	irc_say("Random %sabled", (mode ? "en" : "dis"));
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}

void mpd_stop(void)
{
	if (!mpd.conn) {
		irc_say("Not connected to MPD");
		return;
	}

	mpd_run_noidle(mpd.conn);
	mpd_run_stop(mpd.conn);
	if (!mpd_response_finish(mpd.conn)) {
		mpd_report_error();
		return;
	}
	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
}
