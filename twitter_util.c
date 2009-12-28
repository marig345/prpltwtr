/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#include <string.h>
#include "twitter_util.h"

gchar *get_search_keyword (const gchar *name)
{
    gchar **words = NULL;
    gchar *keyword;

    if (!name ||
        name[0] == '\0' ||
        !g_str_has_prefix (name, SEARCH_PREFIX) ||
        g_strcmp0 (name, SEARCH_PREFIX) == 0)
        return NULL;

    words = g_strsplit (name, SEARCH_PREFIX, 2);
    keyword = g_strdup(words[1]);

    g_strfreev (words);
    return keyword;
}

gchar *xmlnode_get_child_data(const xmlnode *node, const char *name)
{
    xmlnode *child = xmlnode_get_child(node, name);
    if (!child)
        return NULL;
    return xmlnode_get_data_unescaped(child);
}
