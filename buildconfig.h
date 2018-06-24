#define DEVELOPMENT

//#define DEBUG
#ifdef DEBUG
#define NLDEBUG
#endif

#ifndef WPASUPPLICANT_BINARYPATH
#warning "Assuming your wpa_supplicant binary is in /sbin/"
#define WPASUPPLICANT_BINARYPATH "/sbin/wpa_supplicant"
#endif

#ifndef DHCPC_BINARYPATH
#define DHCPC_BINARYPATH "/sbin/dhclient"
#endif

#ifndef DHCPD_BINARYPATH
#define DHCPD_BINARYPATH "/usr/sbin/dhcpd"
#endif
