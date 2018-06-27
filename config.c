#include <json-glib/json-glib.h>
#include "config.h"
#include "utils.h"

static char* cfgpath = "./config.json";
static struct config* cfg = NULL;

#define NETWORKCONFIG "network_config"

static void config_save() {
	JsonBuilder* jsonbuilder = json_builder_new();
	json_builder_begin_object(jsonbuilder);

	if (cfg->ntwkcfg != NULL) {
		json_builder_set_member_name(jsonbuilder, NETWORKCONFIG);
		network_model_config_serialise(cfg->ntwkcfg, jsonbuilder);
		json_builder_end_object(jsonbuilder);
	}

	gsize jsonsz;
	gchar* json = utils_jsonbuildertostring(jsonbuilder, &jsonsz);
	g_file_set_contents(cfgpath, json, jsonsz, NULL);
	g_free(json);
}

void config_init() {
	cfg = g_malloc0(sizeof(*cfg));

	gchar* cfgjson;
	gsize cfgsz;
	gboolean cfgvalid = FALSE;
	if (g_file_get_contents(cfgpath, &cfgjson, &cfgsz, NULL)) {
		JsonParser* parser = json_parser_new();
		if (json_parser_load_from_data(parser, cfgjson, cfgsz, NULL)) {
			JsonNode* root = json_parser_get_root(parser);
			if (JSON_NODE_HOLDS_OBJECT(root)) {
				JsonObject* rootobj = json_node_get_object(root);
				JsonNode* networkconfig = json_object_get_member(rootobj,
				NETWORKCONFIG);
				if (networkconfig != NULL) {
					cfg->ntwkcfg = network_model_config_deserialise(
							networkconfig);
				}
			}
		}
		g_free(cfgjson);
	}

	if (!cfgvalid)
		g_message("config doesn't exist or is invalid");
}

void config_onnetworkconfigured(struct network_config* config) {
	cfg->ntwkcfg = config;
	config_save();
}

const struct config* config_getconfig() {
	return cfg;
}
