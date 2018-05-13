.PHONY: clean

CFLAGS=-ggdb
LIBMICROHTTPD=`pkg-config --cflags --libs libmicrohttpd`
GLIBJSON=`pkg-config --libs --cflags json-glib-1.0`
HOSTAPD=-Ihostap/src/common/ -Ihostap/src/utils/

thingymcconfig: thingymcconfig.c \
	http.o \
	os_unix.o \
	wpa_ctrl.o \
	network.o
	$(CC) $(CFLAGS) $(LIBMICROHTTPD) $(GLIBJSON) -o $@ $^

http.o: http.c http.h
	$(CC) $(CFLAGS) $(LIBMICROHTTPD) $(GLIBJSON) -c -o $@ $<

network.o: network.c network.h
	$(CC) $(CFLAGS) $(GLIBJSON) $(HOSTAPD) -c -o $@ $<


os_unix.o: hostap/src/utils/os_unix.c
	$(CC) $(CFLAGS) -c -o $@ $<

wpa_ctrl.o: hostap/src/common/wpa_ctrl.c
	$(CC) $(CFLAGS) $(HOSTAPD) -DCONFIG_CTRL_IFACE -DCONFIG_CTRL_IFACE_UNIX -c -o $@ $<

clean:
	-rm thingymcconfig *.o
