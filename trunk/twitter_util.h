/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#ifndef _TWITTER_UTIL_H_
#define _TWITTER_UTIL_H_

#include <string.h>
#include <glib.h>
#include <xmlnode.h>
#include <debug.h>

#include "config.h"
#include "twitter_xml.h"
#include "twitter_prefs.h" //TODO move

#define TWITTER_URI_ACTION_USER		"user" //TODO: move?
#define TWITTER_URI_ACTION_SEARCH	"search" //TODO: move?

long long purple_account_get_long_long(PurpleAccount *account, const gchar *key, long long default_value);
void purple_account_set_long_long(PurpleAccount *account, const gchar *key, long long value);

gchar *twitter_utf8_find_last_pos(const gchar *str, const gchar *needles, glong str_len);
char *twitter_utf8_get_segment(const gchar *message, int max_len, const gchar *add_text, const gchar **new_start);
GArray *twitter_utf8_get_segments(const gchar *message, int segment_length, const gchar *add_text);

#ifndef g_slice_new0
#define g_slice_new0(a) g_new0(a, 1)
#define g_slice_free(a, b) g_free(b)
#endif

#ifndef g_strcmp0
#define g_strcmp0(a, b) (a == NULL && b == NULL ? 0 : a == NULL ? -1 : b == NULL ? 1 : strcmp(a, b))
#endif

//TODO: move this?
char *twitter_format_tweet(PurpleAccount *account, const char *src_user, const char *message, long long id, gboolean allow_link);
#endif /* UTIL_H_ */