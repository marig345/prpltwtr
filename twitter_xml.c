#include "twitter_xml.h"

static time_t twitter_get_timezone_offset()
{
	static long tzoff = PURPLE_NO_TZ_OFF;
	if (tzoff == PURPLE_NO_TZ_OFF)
	{
		struct tm t;
		time_t tval = 0;
		const char *tzoff_str;
		long tzoff_l;

		tzoff = 0;

		time(&tval);
		localtime_r(&tval, &t);
		tzoff_str = purple_get_tzoff_str(&t, FALSE);
		if (tzoff_str && tzoff_str[0] != '\0')
		{
			tzoff_l = strtol(tzoff_str, NULL, 10);
			tzoff = (60 * 60 * (int) (tzoff_l / 100)) +
				(60 * (tzoff_l % 100));
		}
	}
	return tzoff;
}
static time_t twitter_status_parse_timestamp(const char *timestamp)
{
	//Sat Mar 07 18:12:10 +0000 2009
	char month_str[4], tz_str[6];
	char day_name[4];
	char *tz_ptr = tz_str;
	static const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL
	};
	time_t tval = 0;
	struct tm t;
	memset(&t, 0, sizeof(t));

	time(&tval);
	localtime_r(&tval, &t);

	if (sscanf(timestamp, "%03s %03s %02d %02d:%02d:%02d %05s %04d",
				day_name, month_str, &t.tm_mday,
				&t.tm_hour, &t.tm_min, &t.tm_sec,
				tz_str, &t.tm_year) == 8) {
		gboolean offset_positive = TRUE;
		int tzhrs;
		int tzmins;

		for (t.tm_mon = 0;
				months[t.tm_mon] != NULL &&
				strcmp(months[t.tm_mon], month_str) != 0; t.tm_mon++);
		if (months[t.tm_mon] != NULL) {
			if (*tz_str == '-') {
				offset_positive = FALSE;
				tz_ptr++;
			} else if (*tz_str == '+') {
				tz_ptr++;
			}

			t.tm_year -= 1900;

			if (sscanf(tz_ptr, "%02d%02d", &tzhrs, &tzmins) == 2) {
				time_t tzoff = tzhrs * 60 * 60 + tzmins * 60;
				time_t returned_time;
				tzoff += twitter_get_timezone_offset();

				returned_time = mktime(&t);
				if (returned_time != -1 && returned_time != 0)
					return returned_time + tzoff;
			}
		}
	}

	purple_debug_info(TWITTER_PROTOCOL_ID, "Can't parse timestamp %s\n", timestamp);
	return tval;
}
TwitterUserData *twitter_user_node_parse(xmlnode *user_node)
{
	TwitterUserData *user;

	if (user_node == NULL)
		return NULL;

	user = g_new0(TwitterUserData, 1);
	user->screen_name = xmlnode_get_child_data(user_node, "screen_name");

	if (!user->screen_name)
	{
		g_free(user);
		return NULL;
	}

	user->name = xmlnode_get_child_data(user_node, "name");
	user->profile_image_url = xmlnode_get_child_data(user_node, "profile_image_url");
	user->description = xmlnode_get_child_data(user_node, "description");

	return user;
}
TwitterStatusData *twitter_status_node_parse(xmlnode *status_node)
{
	TwitterStatusData *status;
	char *data;

	if (status_node == NULL)
		return NULL;

	status = g_new0(TwitterStatusData, 1);
	status->text = xmlnode_get_child_data(status_node, "text");

	if ((data = xmlnode_get_child_data(status_node, "created_at")))
	{
		time_t created_at = twitter_status_parse_timestamp(data);
		status->created_at = created_at ? created_at : time(NULL);
		g_free(data);
	}

	if ((data = xmlnode_get_child_data(status_node, "id")))
	{
		status->id = strtoll(data, NULL, 10);
		g_free(data);
	}

	if ((data = xmlnode_get_child_data(status_node, "in_reply_to_status_id"))) {
		status->in_reply_to_status_id = strtoll (data, NULL, 10);
		g_free (data);
	}

	status->in_reply_to_screen_name =
		xmlnode_get_child_data(status_node, "in_reply_to_screen_name");

	return status;

}

GList *twitter_users_node_parse(xmlnode *users_node)
{
	GList *users = NULL;
	xmlnode *user_node;
	for (user_node = users_node->child; user_node; user_node = user_node->next)
	{
		if (user_node->name && !strcmp(user_node->name, "user"))
		{
			TwitterBuddyData *data = g_new0(TwitterBuddyData, 1);

			xmlnode *status_node = xmlnode_get_child(user_node, "status");

			data->user = twitter_user_node_parse(user_node);
			data->status = twitter_status_node_parse(status_node);

			users = g_list_append(users, data);
		}
	}
	return users;
}
GList *twitter_users_nodes_parse(GList *nodes)
{
	GList *l_users_data = NULL;
	GList *l;
	for (l = nodes; l; l = l->next)
	{
		xmlnode *node = l->data;
		l_users_data = g_list_concat(twitter_users_node_parse(node), l_users_data);
	}
	return l_users_data;
}

GList *twitter_statuses_node_parse(xmlnode *statuses_node)
{
	GList *statuses = NULL;
	xmlnode *status_node;

	for (status_node = statuses_node->child; status_node; status_node = status_node->next)
	{
		if (status_node->name && !strcmp(status_node->name, "status"))
		{
			TwitterBuddyData *data = g_new0(TwitterBuddyData, 1);
			xmlnode *user_node = xmlnode_get_child(status_node, "user");
			data->user = twitter_user_node_parse(user_node);
			data->status = twitter_status_node_parse(status_node);
			statuses = g_list_prepend(statuses, data);
		}
	}

	return statuses;
}

GList *twitter_statuses_nodes_parse(GList *nodes)
{
	GList *l_users_data = NULL;
	GList *l;
	for (l = nodes; l; l = l->next)
	{
		xmlnode *node = l->data;
		l_users_data = g_list_concat(twitter_statuses_node_parse(node), l_users_data);
	}
	return l_users_data;
}
