#ifndef _TWITTER_ENDPOINT_REPLY_H_
#define _TWITTER_ENDPOINT_REPLY_H_

#include "config.h"

#include "twitter_endpoint_im.h"
#include "twitter_conn.h"
#include "twitter_buddy.h"

TwitterEndpointImSettings *twitter_endpoint_reply_get_settings();
PurpleConversation *twitter_endpoint_reply_conversation_new(TwitterEndpointIm *im,
		const gchar *buddy_name,
		long long reply_id);

#endif