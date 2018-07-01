#include "buildconfig.h"
#include "network_wpasupplicant.h"
#include "network_wpasupplicant_priv.h"
#include "network_priv.h"

#define ISOK(rsp) (strcmp(rsp, "OK") == 0)

static GPtrArray* scanresults = NULL;
static char* wpasupplicantsocketdir = "/tmp/thingy_sockets/";
static gboolean connected = FALSE;
static gchar* lasterror = NULL;

typedef void (*wpaeventhandler)(struct wpa_ctrl* wpa_ctrl, const gchar* event);
struct wpaeventhandler_entry {
	const gchar* command;
	const wpaeventhandler handler;
};

static gchar* network_wpasupplicant_docommand(struct wpa_ctrl* wpa_ctrl,
		gsize* replylen, gboolean stripnewline, const char* command) {
	*replylen = 1024;
	char* reply = g_malloc0(*replylen + 1);
	if (wpa_ctrl_request(wpa_ctrl, command, strlen(command), reply, replylen,
	NULL) == 0) {
		if (stripnewline) {
			//kill the newline
			reply[*replylen - 1] = '\0';
			*replylen -= 1;
		}
#ifdef WSDEBUG
		g_message("command: %s, response: %s", command, reply);
#endif

		return reply;
	} else {
		g_free(reply);
		return NULL;
	}
}

static gchar* network_wpasupplicant_docommandf(struct wpa_ctrl* wpa_ctrl,
		gsize* replylen, gboolean stripnewline, const char* format, ...) {
	va_list fmtargs;
	va_start(fmtargs, format);
	GString* cmdstr = g_string_new(NULL);
	g_string_vprintf(cmdstr, format, fmtargs);
	gchar* cmd = g_string_free(cmdstr, FALSE);
	va_end(fmtargs);
	gchar* reply = network_wpasupplicant_docommand(wpa_ctrl, replylen,
			stripnewline, cmd);
	g_free(cmd);
	return reply;
}

static unsigned network_wpasupplicant_getscanresults_flags(const char* flags) {
	unsigned f = 0;
	GRegex* flagregex = g_regex_new(MATCHFLAG, 0, 0, NULL);
	GMatchInfo* flagsmatchinfo;
	g_regex_match(flagregex, flags, 0, &flagsmatchinfo);
	while (g_match_info_matches(flagsmatchinfo)) {
		char* flag = g_match_info_fetch(flagsmatchinfo, 1);
		if (strcmp(flag, FLAG_ESS) == 0)
			f |= NF_ESS;
		else if (strcmp(flag, FLAG_WPS) == 0)
			f |= NF_WPS;
		else if (strcmp(flag, FLAG_WEP) == 0)
			f |= NF_WEP;
		else if (strcmp(flag, FLAG_WPA_PSK_CCMP) == 0)
			f |= NF_WPA_PSK_CCMP;
		else if (strcmp(flag, FLAG_WPA_PSK_CCMP_TKIP) == 0)
			f |= NF_WPA_PSK_CCMP_TKIP;
		else if (strcmp(flag, FLAG_WPA2_PSK_CCMP) == 0)
			f |= NF_WPA2_PSK_CCMP;
		else if (strcmp(flag, FLAG_WPA2_PSK_CCMP_TKIP) == 0)
			f |= NF_WPA2_PSK_CCMP_TKIP;
		else
			g_message("unhandled flag: %s", flag);
		g_free(flag);
		g_match_info_next(flagsmatchinfo, NULL);
	}
	g_match_info_free(flagsmatchinfo);
	g_regex_unref(flagregex);
	return f;
}

static void network_wpasupplicant_freescanresult(gpointer data) {
	g_free(data);
}

static void network_wpasupplicant_getscanresults(struct wpa_ctrl* wpa_ctrl,
		const gchar* event) {
	if (scanresults != NULL)
		g_ptr_array_unref(scanresults);

	scanresults = g_ptr_array_new_with_free_func(
			network_wpasupplicant_freescanresult);

	size_t replylen;
	char* reply = network_wpasupplicant_docommand(wpa_ctrl, &replylen, FALSE,
			"SCAN_RESULTS");
	if (reply != NULL) {
		//g_message("%s\n sr: %s", SCANRESULTREGEX, reply);
		GRegex* networkregex = g_regex_new(SCANRESULTREGEX, 0, 0, NULL);
		GMatchInfo* matchinfo;
		g_regex_match(networkregex, reply, 0, &matchinfo);
		while (g_match_info_matches(matchinfo)) {
			//char* line = g_match_info_fetch(matchinfo, 0);
			//g_message("l %s", line);

			char* bssid = g_match_info_fetch(matchinfo, 1);

			char* frequencystr = g_match_info_fetch(matchinfo, 2);
			int frequency = g_ascii_strtoll(frequencystr, NULL, 10);
			char* rssistr = g_match_info_fetch(matchinfo, 3);
			int rssi = g_ascii_strtoll(rssistr, NULL, 10);
			char* flags = g_match_info_fetch(matchinfo, 4);
			char* ssid = g_match_info_fetch(matchinfo, 5);

			struct network_scanresult* n = g_malloc0(
					sizeof(struct network_scanresult));
			strncpy(n->bssid, bssid, sizeof(n->bssid));
			n->frequency = frequency;
			n->rssi = rssi;
			n->flags = network_wpasupplicant_getscanresults_flags(flags);
			strncpy(n->ssid, ssid, sizeof(n->ssid));
			g_ptr_array_add(scanresults, n);

			g_free(bssid);
			g_free(frequencystr);
			g_free(rssistr);
			g_free(flags);
			g_free(ssid);

#ifdef WSDEBUG
			g_message("bssid %s, frequency %d, rssi %d, flags %u, ssid %s",
					n->bssid, n->frequency, n->rssi, n->flags, n->ssid);
#endif
			g_match_info_next(matchinfo, NULL);
		}
		g_match_info_free(matchinfo);
		g_regex_unref(networkregex);
	}
}

static void network_wpasupplicant_eventhandler_connect(
		struct wpa_ctrl* wpa_ctrl, const gchar* event) {
	connected = TRUE;
	network_onsupplicantstatechange(connected);
}

static void network_wpasupplicant_eventhandler_disconnect(
		struct wpa_ctrl* wpa_ctrl, const gchar* event) {
	connected = FALSE;
	network_onsupplicantstatechange(connected);
}

static GHashTable* network_wpasupplicant_getkeyvalues(const gchar* event) {
	GHashTable* result = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			g_free);

	GRegex* keyvalueregex = g_regex_new(NETWORK_WPASUPPLICANT_REGEX_KEYVALUE, 0,
			0, NULL);
	GMatchInfo* matchinfo;
	if (g_regex_match(keyvalueregex, event, 0, &matchinfo)) {
		do {
			gchar* key = g_match_info_fetch(matchinfo, 1);
			gchar* value = g_match_info_fetch(matchinfo, 2);
			if (key != NULL && value != NULL) {
				//g_message("kv: %s=%s", key, value);
				g_hash_table_insert(result, key, value);
			}
		} while (g_match_info_next(matchinfo, NULL));
	}
	g_match_info_free(matchinfo);

	return result;
}

static void network_wpasupplicant_eventhandler_ssiddisabled(
		struct wpa_ctrl* wpa_ctrl, const gchar* event) {
	//<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid="ghettonet" auth_failures=2 duration=23 reason=WRONG_KEY
	g_message("here");
	GHashTable* keyvalues = network_wpasupplicant_getkeyvalues(event);
	gchar* reason = g_hash_table_lookup(keyvalues, "reason");
	if (lasterror != NULL)
		g_free(lasterror);
	lasterror = g_strdup(reason);
	g_hash_table_unref(keyvalues);
}

static const struct wpaeventhandler_entry eventhandlers[] = { {
WPA_EVENT_SCAN_RESULTS, network_wpasupplicant_getscanresults }, {
WPA_EVENT_CONNECTED, network_wpasupplicant_eventhandler_connect }, {
WPA_EVENT_DISCONNECTED, network_wpasupplicant_eventhandler_disconnect }, {
WPA_EVENT_TEMP_DISABLED, network_wpasupplicant_eventhandler_ssiddisabled } };

static gboolean network_wpasupplicant_onevent(GIOChannel *source,
		GIOCondition condition, gpointer data) {
	struct wpa_ctrl* wpa_ctrl = data;
	size_t replylen = 1024;
	char* reply = g_malloc0(replylen + 1);
	wpa_ctrl_recv(wpa_ctrl, reply, &replylen);

#ifdef WSDEBUG
	g_message("event for wpa supplicant: %s", reply);
#endif

	GRegex* regex = g_regex_new("^<([0-4])>([A-Z,-]* )", 0, 0, NULL);
	GMatchInfo* matchinfo;
	if (g_regex_match(regex, reply, 0, &matchinfo)) {
		char* level = g_match_info_fetch(matchinfo, 1);
		char* command = g_match_info_fetch(matchinfo, 2);

#ifdef WSDEBUG
		g_message("level: %s, command %s", level, command);
#endif

		for (int i = 0; i < G_N_ELEMENTS(eventhandlers); i++) {
			if (strcmp(command, eventhandlers[i].command) == 0) {
				eventhandlers[i].handler(wpa_ctrl, reply);
				break;
			}
		}

		g_free(level);
		g_free(command);

	}
	g_match_info_free(matchinfo);
	g_regex_unref(regex);

	g_free(reply);
	return TRUE;
}

void network_wpasupplicant_scan(struct wpa_ctrl* wpa_ctrl) {
	size_t replylen;
	char* reply = network_wpasupplicant_docommand(wpa_ctrl, &replylen, TRUE,
			"SCAN");
	if (reply != NULL) {
		g_free(reply);
	}
}

int network_wpasupplicant_addnetwork(struct wpa_ctrl* wpa_ctrl,
		const gchar* ssid, const gchar* psk, unsigned mode) {
	g_message("adding network %s with psk %s", ssid, psk);
	gsize respsz;
	gchar* resp = network_wpasupplicant_docommand(wpa_ctrl, &respsz, TRUE,
			"ADD_NETWORK");
	guint64 networkid;
	if (resp != NULL) {
		if (!g_ascii_string_to_unsigned(resp, 10, 0, G_MAXUINT8, &networkid,
		NULL)) {
			g_message("failed to parse network id, command failed?");
			return -1;
		}
		g_free(resp);
	}

	resp = network_wpasupplicant_docommandf(wpa_ctrl, &respsz, TRUE,
			"SET_NETWORK %u ssid \"%s\"", (unsigned) networkid, ssid);
	if (resp != NULL) {
		g_assert(ISOK(resp));
		g_free(resp);
	}

	resp = network_wpasupplicant_docommandf(wpa_ctrl, &respsz, TRUE,
			"SET_NETWORK %u psk \"%s\"", (unsigned) networkid, psk);
	if (resp != NULL) {
		g_assert(ISOK(resp));
		g_free(resp);
	}

	resp = network_wpasupplicant_docommandf(wpa_ctrl, &respsz, TRUE,
			"SET_NETWORK %u mode %d", (unsigned) networkid, mode);
	if (resp != NULL) {
		g_assert(ISOK(resp));
		g_free(resp);
	}

	return networkid;
}

void network_wpasupplicant_selectnetwork(struct wpa_ctrl* wpa_ctrl, int which) {
	gsize respsz;
	gchar* resp = network_wpasupplicant_docommandf(wpa_ctrl, &respsz, TRUE,
			"SELECT_NETWORK %d", which);
	if (resp != NULL)
		g_free(resp);
}

void network_wpasupplicant_init() {
}

gboolean network_wpasupplicant_start(struct wpa_ctrl** wpa_ctrl,
		struct wpa_ctrl** wpa_event, const char* interface, GPid* pid) {
	gboolean ret = FALSE;
	g_message("starting wpa_supplicant for %s", interface);
	gchar* args[] = { WPASUPPLICANT_BINARYPATH, "-Dnl80211", "-i", interface,
			"-C", wpasupplicantsocketdir, "-qq", NULL };
	if (!g_spawn_async(NULL, args, NULL,
			G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL,
			NULL, pid, NULL)) {
		g_message("failed to start wpa_supplicant");
		goto err_spawn;
	}

	g_usleep(2 * 1000000);

	GString* socketpathstr = g_string_new(NULL);
	g_string_printf(socketpathstr, "%s/%s", wpasupplicantsocketdir, interface);
	gchar* socketpath = g_string_free(socketpathstr, FALSE);

	*wpa_ctrl = wpa_ctrl_open(socketpath);
	if (*wpa_ctrl)
		g_message("wpa_supplicant control socket connected");
	else {
		g_message("failed to open wpa_supplicant control socket");
		goto err_openctrlsck;
	}

	*wpa_event = wpa_ctrl_open(socketpath);
	if (*wpa_event) {
		g_message("wpa_supplicant event socket connected");
		wpa_ctrl_attach(*wpa_event);
		int fd = wpa_ctrl_get_fd(*wpa_event);
		GIOChannel* channel = g_io_channel_unix_new(fd);
		g_io_add_watch(channel, G_IO_IN, network_wpasupplicant_onevent,
				*wpa_event);
	} else {
		g_message("failed to open wpa_supplicant event socket");
		goto err_openevntsck;
	}

	ret = TRUE;
	goto out;

	err_openevntsck:	//
	wpa_ctrl_close(*wpa_ctrl);
	out: //
	err_openctrlsck:	//
	g_free(socketpath);
	err_spawn:			//
	return ret;
}

GPtrArray* network_wpasupplicant_getlastscanresults() {
	return scanresults;
}

void network_wpasupplicant_stop(struct wpa_ctrl* wpa_ctrl, GPid* pid) {
	wpa_ctrl_close(wpa_ctrl);
}

void network_wpasupplicant_dumpstatus(JsonBuilder* builder) {
	json_builder_set_member_name(builder, "supplicant");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "connected");
	json_builder_add_boolean_value(builder, connected);

	if (lasterror != NULL) {
		json_builder_set_member_name(builder, "lasterror");
		json_builder_add_string_value(builder, lasterror);
	}

	json_builder_end_object(builder);
}
