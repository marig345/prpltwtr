#ifndef _TWITTER_TWEETACTIONS_H_
#define _TWITTER_TWEETACTIONS_H_

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

void twitter_tweet_actions_account_load(PurpleAccount *account);
void twitter_tweet_actions_account_unload(PurpleAccount *account);

#endif

#endif
