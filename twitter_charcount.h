#ifndef _TWITTER_CHARCOUNT_H_
#define _TWITTER_CHARCOUNT_H_

#if _HAVE_PIDGIN_

#include <glib.h>
#include <conversation.h>

void twitter_charcount_detach_from_all_windows();
void twitter_charcount_attach_to_all_windows();
void twitter_charcount_conv_created_cb(PurpleConversation *conv, gpointer null);

#endif

#endif
