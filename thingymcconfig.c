#include <glib.h>
#include "config.h"
#include "network.h"
#include "http.h"

int main(int argc, char** argv) {

	GMainLoop* mainloop = g_main_loop_new(NULL, FALSE);

	config_init();
	network_init();
	network_start();
	http_start();

	g_main_loop_run(mainloop);

	return 0;
}
