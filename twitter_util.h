/**
 * Copyright (C) 2009 Tan Miaoqing
 * Contact: Tan Miaoqing <rabbitrun84@gmail.com>
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <glib.h>
#include <xmlnode.h>

#define SEARCH_PREFIX "#"

gchar *get_search_keyword (const gchar *name);

gchar *xmlnode_get_child_data(const xmlnode *node, const char *name);

#endif /* UTIL_H_ */
