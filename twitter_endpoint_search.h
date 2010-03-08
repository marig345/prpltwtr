#ifndef _TWITTER_ENDPOINT_SEARCH_H_
#define _TWITTER_ENDPOINT_SEARCH_H_

#include "config.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <glib.h>

#include <account.h>
#include <accountopt.h>
#include <blist.h>
#include <cmds.h>
#include <conversation.h>
#include <connection.h>
#include <core.h>
#include <debug.h>
#include <notify.h>
#include <privacy.h>
#include <prpl.h>
#include <roomlist.h>
#include <status.h>
#include <util.h>
#include <version.h>
#include <cipher.h>
#include <sslconn.h>
#include <request.h>

#include "twitter_endpoint_chat.h"
#include "twitter_prefs.h"
#include "twitter_api.h"

typedef struct
{
	gchar *search_text; /* e.g. N900 */
	gchar *refresh_url; /* e.g. ?since_id=6276370030&q=n900 */

	long long last_tweet_id;
} TwitterSearchTimeoutContext;

TwitterEndpointChatSettings *twitter_endpoint_search_get_settings();

#endif
