#include "buildconfig.h"
#include "network_dhcp.h"
#include "network_rtnetlink.h"
#include "dhcp4_client.h"
#include "dhcp4_server.h"
#include "ip4.h"

static struct dhcp4_client_cntx* dhcp4clientcntx = NULL;
static struct dhcp4_server_cntx* dhcp4servercntx;

void _dhcp4_client_clearinterface(unsigned ifidx) {
	network_rtnetlink_clearipv4addr(ifidx);
}

void _dhcp4_client_configureinterface(unsigned ifidx, const guint8* address,
		const guint8* netmask, const guint8* gateway, const guint8* nameservers,
		const guint8 numnameservers) {

	guint32 nm = *((guint32*) netmask);
	int nmbits = 0;
	for (int i = 0; i < sizeof(nm) * 8; i++)
		nmbits += (nm >> i) & 1;

	GString* addrgstr = g_string_new(NULL);
	g_string_append_printf(addrgstr, IP4_ADDRFMT"/%d", IP4_ARGS(address),
			nmbits);
	gchar* addrstr = g_string_free(addrgstr, FALSE);
	network_rtnetlink_setipv4addr(ifidx, addrstr);
	g_free(addrstr);

	network_rtnetlink_setipv4defaultfw(ifidx, gateway);
}

void network_dhcpclient_start(unsigned ifidx, const gchar* interfacename,
		const guint8* interfacemac) {
	g_message("starting dhcp4 client for %s", interfacename);
	dhcp4clientcntx = dhcp4_client_new(ifidx, interfacemac);
	dhcp4_client_start(dhcp4clientcntx);
}

void network_dhcpclient_stop() {
	dhcp4_client_stop(dhcp4clientcntx);
}

void network_dhcpserver_start(unsigned ifidx, const gchar* interfacename,
		const guint8* interfacemac) {
	guint8 serverid[] = { 10, 0, 0, 1 };
	guint8 poolstart[] = { 10, 0, 0, 2 };
	unsigned poollen = 5;
	guint8 subnetmask[] = { 255, 255, 255, 248 };
	guint8 defaultgw[] = { 10, 0, 0, 1 };
	g_message("starting dhcp server for %s", interfacename);
	dhcp4servercntx = dhcp4_server_new(ifidx, interfacemac, serverid, poolstart,
			poollen, subnetmask, defaultgw);
	dhcp4_server_start(dhcp4servercntx);
}

void network_dhcpserver_stop() {
	dhcp4_server_stop(dhcp4servercntx);
}

static gchar* dhcpcstatestrs[] = { [DHCP4CS_IDLE] = "idle", [DHCP4CS_DISCOVERING
		] = "discovering", [DHCP4CS_REQUESTING ] = "requesting",
		[DHCP4CS_CONFIGURED] = "configured" };

static void network_dhcp_dumpstatus_addip4addr(JsonBuilder* builder,
		guint8* addr) {
	GString* strstr = g_string_new(NULL);
	g_string_printf(strstr, IP4_ADDRFMT, IP4_ARGS(addr));
	gchar* str = g_string_free(strstr, FALSE);
	json_builder_add_string_value(builder, str);
	g_free(str);
}

void network_dhcp_dumpstatus(JsonBuilder* builder) {
	if (dhcp4clientcntx != NULL) {
		json_builder_set_member_name(builder, "dhcp4");
		json_builder_begin_object(builder);

		json_builder_set_member_name(builder, "state");
		json_builder_add_string_value(builder,
				dhcpcstatestrs[dhcp4clientcntx->state]);

		if (dhcp4clientcntx->currentlease != NULL) {
			json_builder_set_member_name(builder, "lease");
			json_builder_begin_object(builder);
			json_builder_set_member_name(builder, "ip");
			network_dhcp_dumpstatus_addip4addr(builder,
					dhcp4clientcntx->currentlease->leasedip);
			json_builder_set_member_name(builder, "subnetmask");
			network_dhcp_dumpstatus_addip4addr(builder,
					dhcp4clientcntx->currentlease->subnetmask);
			json_builder_set_member_name(builder, "defaultgw");
			network_dhcp_dumpstatus_addip4addr(builder,
					dhcp4clientcntx->currentlease->defaultgw);
			json_builder_set_member_name(builder, "nameservers");
			json_builder_begin_array(builder);
			for (int i = 0; i < dhcp4clientcntx->currentlease->numnameservers;
					i++) {
				network_dhcp_dumpstatus_addip4addr(builder,
						dhcp4clientcntx->currentlease->nameservers[i]);
			}
			json_builder_end_array(builder);
			json_builder_end_object(builder);
		}
		json_builder_end_object(builder);
	}
}
