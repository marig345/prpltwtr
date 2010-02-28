#ifndef _TWITTER_CONVICON_H_
#define _TWITTER_CONVICON_H_

#if _HAVE_PIDGIN_
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <time.h>
#include <locale.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gtkplugin.h>
#include <util.h>
#include <debug.h>
#include <connection.h>
#include <version.h>
#include <sound.h>
#include <gtkconv.h>
#include <gtkimhtml.h>
#include <core.h>

typedef struct {
	GdkPixbuf *pixbuf;      /* icon pixmap */
	gboolean requested;     /* TRUE if download icon has been requested */
	GList *request_list;    /* marker list */
	PurpleUtilFetchUrlData *fetch_data; /* icon fetch data */
	gchar *icon_url;        /* url for the user's icon */
	gint use_count;         /* usage count */
	time_t mtime;           /* mtime of file */
} TwitterConvIcon;

void twitter_conv_icon_init(PurplePlugin *plugin);
void twitter_conv_icon_account_load(PurpleAccount *account);
void twitter_conv_icon_account_unload(PurpleAccount *account);

void twitter_conv_icon_got_user_icon(PurpleAccount *account, const char *user_name, const gchar *url, time_t icon_time);
void twitter_conv_icon_got_buddy_icon(PurpleAccount *account, const char *user_name, PurpleBuddyIcon *buddy_icon);


#endif

#endif
