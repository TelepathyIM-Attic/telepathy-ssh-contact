# ssh plugin
plugindir = $(libdir)/vinagre-1/plugins

AM_CPPFLAGS =			\
	$(ERROR_CFLAGS)		\
	$(SSH_CONTACT_CFLAGS)	\
	$(VINAGRE_CFLAGS) 	\
	$(NULL)

plugin_LTLIBRARIES = libssh-contact.la

libssh_contact_la_LIBADD =	\
	$(SSH_CONTACT_LIBS)	\
	$(VINAGRE_LIBS) 	\
	$(NULL)

libssh_contact_la_SOURCES =			\
	plugin.h plugin.c			\
	connection.h connection.c		\
	tab.h tab.c				\
	tree-view.h tree-view.c			\
	../client-helpers.h ../client-helpers.c	\
	$(NULL)

plugin_in_files = ssh-contact.vinagre-plugin.desktop.in

ssh-contact.vinagre-plugin: ssh-contact.vinagre-plugin.desktop.in
	$(AM_V_GEN) $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c \
		$(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.vinagre-plugin.desktop.in=.vinagre-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

