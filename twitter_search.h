/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#ifndef _TWITTER_SEARCH_H_
#define _TWITTER_SEARCH_H_

#include <glib.h>
#include "twitter_xml.h"
#include "twitter_request.h"

typedef struct _TwitterSearchErrorData TwitterSearchErrorData;

struct _TwitterSearchErrorData
{

};

/* @search_result: an array of TwitterUserTweet */
typedef void (*TwitterSearchSuccessFunc)(PurpleAccount *account,
        const GArray *search_results,
        const gchar *refresh_url,
        long long max_id,
        gpointer user_data);

typedef gboolean (*TwitterSearchErrorFunc)(PurpleAccount *account,
        const TwitterSearchErrorData *error_data,
        gpointer user_data);

void twitter_search(PurpleAccount *account, TwitterRequestParams *params,
		TwitterSearchSuccessFunc success_cb, TwitterSearchErrorFunc error_cb,
		gpointer data);

#endif /* TWITTER_SEARCH_H_ */
