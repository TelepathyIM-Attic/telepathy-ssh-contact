AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(TELEPATHY_SSH_CFLAGS)
AM_CFLAGS = -Wall -g
AM_LDFLAGS = -Wl,--export-dynamic
LDADD = $(TELEPATHY_SSH_LIBS)

bin_PROGRAMS = telepathy-ssh-client telepathy-ssh-service

telepathy_ssh_client_SOURCES = \
	common.c common.h \
	client.c

telepathy_ssh_service_SOURCES = \
	common.c common.h \
	service.c

servicefiledir = $(datadir)/dbus-1/services
servicefile_in_files = \
	org.freedesktop.Telepathy.Client.TelepathySSH.service.in

servicefile_DATA = $(servicefile_in_files:.service.in=.service)

org.freedesktop.Telepathy.Client.TelepathySSH.service: org.freedesktop.Telepathy.Client.TelepathySSH.service.in
	$(AM_V_GEN)sed -e "s|[@]bindir[@]|$(bindir)|" $< > $@

clientfiledir = $(datarootdir)/telepathy/clients
clientfile_DATA = \
	TelepathySSH.client

EXTRA_DIST = 				\
	$(clientfile_DATA)		\
	$(servicefile_in_files)		\
	$(NULL)

DISTCLEANFILES = 		\
	$(servicefile_DATA)	\
	$(NULL)

