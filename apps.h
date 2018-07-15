#pragma once

#include <json-glib/json-glib.h>

struct apps_appstateupdate {
	unsigned char appindex;
	unsigned char appstate;
	unsigned char apperror;
	unsigned char connectivitystate;
	unsigned char connectivityerror;
};

void apps_init(const gchar** appnames);
void apps_onappstateupdate(const struct apps_appstateupdate* update);
void apps_dumpstatus(JsonBuilder* builder);
