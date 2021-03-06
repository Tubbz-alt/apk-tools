/* app_update.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2008 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include "apk_defines.h"
#include "apk_applet.h"
#include "apk_database.h"
#include "apk_version.h"
#include "apk_print.h"

static int update_main(void *ctx, struct apk_ctx *ac, struct apk_string_array *args)
{
	struct apk_out *out = &ac->out;
	struct apk_database *db = ac->db;
	struct apk_repository *repo;
	int i;
	char buf[32] = "OK:";

	if (apk_out_verbosity(out) < 1)
		return db->repo_update_errors;

	for (i = 0; i < db->num_repos; i++) {
		repo = &db->repos[i];

		if (APK_BLOB_IS_NULL(repo->description))
			continue;

		apk_msg(out, BLOB_FMT " [%s]",
			BLOB_PRINTF(repo->description),
			db->repos[i].url);
	}

	if (db->repo_update_errors != 0)
		snprintf(buf, sizeof(buf), "%d errors;",
			 db->repo_update_errors);
	apk_msg(out, "%s %d distinct packages available", buf,
		db->available.packages.num_items);

	return db->repo_update_errors;
}

static struct apk_applet apk_update = {
	.name = "update",
	.open_flags = APK_OPENF_WRITE,
	.forced_force = APK_FORCE_REFRESH,
	.main = update_main,
};

APK_DEFINE_APPLET(apk_update);

