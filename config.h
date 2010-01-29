#ifndef G_GNUC_NULL_TERMINATED
#  if __GNUC__ >= 4
#    define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#  else
#    define G_GNUC_NULL_TERMINATED
#  endif /* __GNUC__ >= 4 */
#endif /* G_GNUC_NULL_TERMINATED */

#ifdef _WIN32
#	include <win32dep.h>
#endif


/* The number of tweets to return per page, up to a max of 100 */
#define TWITTER_SEARCH_RPP_DEFAULT 20

#define TWITTER_INITIAL_REPLIES_COUNT 100
#define TWITTER_INITIAL_DMS_COUNT 100
#define TWITTER_EVERY_REPLIES_COUNT 100
#define TWITTER_EVERY_DMS_COUNT 100

#define TWITTER_TIMELINE_REFRESH_DEFAULT 1

#define TWITTER_HOME_TIMELINE_INITIAL_COUNT 20
#define TWITTER_HOME_TIMELINE_PAGE_COUNT 100

#define TWITTER_PROTOCOL_ID "prpl-twitter"

#define TWITTER_OAUTH_KEY "hrL4RlfT8MVOWbDdeY0EQ"
#define TWITTER_OAUTH_SECRET "QXromX9owFx7Gx0ma8LK0fApX0kVqYf1CXWuGRyuP4"

/* We'll handle TWITTER_URI://foo as an internal action */
#define TWITTER_URI "prpltwtr"

#define MAX_TWEET_LENGTH 140

#define TWITTER_STATUS_ONLINE	"online"
#define TWITTER_STATUS_OFFLINE	"offline"

