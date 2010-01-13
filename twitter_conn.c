#include "twitter_conn.h"

void twitter_connection_foreach_endpoint_im(TwitterConnectionData *twitter,
		void (*cb)(TwitterConnectionData *twitter, TwitterEndpointIm *im, gpointer data),
		gpointer data)
{
	int i;
	for (i = 0; i < TWITTER_IM_TYPE_UNKNOWN; i++)
		if (twitter->endpoint_ims[i])
			cb(twitter, twitter->endpoint_ims[i], data);
}
