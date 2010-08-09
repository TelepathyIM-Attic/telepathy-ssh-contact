AM_CPPFLAGS =			\
	$(ERROR_CFLAGS)		\
	$(SSH_CONTACT_CFLAGS)	\
	$(NULL)

LDADD =				\
	$(SSH_CONTACT_LIBS)	\
	$(NULL)

bin_PROGRAMS = ssh-contact
libexec_PROGRAMS = ssh-contact-service

ssh_contact_SOURCES = \
	common.c common.h \
	client-helpers.c client-helpers.h \
	client.c

ssh_contact_service_SOURCES = \
	common.c common.h \
	service.c

servicefiledir = $(datadir)/dbus-1/services
servicefile_in_files = \
	org.freedesktop.Telepathy.Client.SSHContact.service.in

servicefile_DATA = $(servicefile_in_files:.service.in=.service)

org.freedesktop.Telepathy.Client.SSHContact.service: org.freedesktop.Telepathy.Client.SSHContact.service.in
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

clientfiledir = $(datarootdir)/telepathy/clients
clientfile_DATA = \
	SSHContact.client

EXTRA_DIST = 				\
	$(clientfile_DATA)		\
	$(servicefile_in_files)		\
	$(NULL)

DISTCLEANFILES = 		\
	$(servicefile_DATA)	\
	$(NULL)

