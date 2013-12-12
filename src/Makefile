
builddir=.
top_srcdir=/usr/local/apache24
top_builddir=$(top_srcdir)
include $(top_builddir)/build/special.mk

#   the used tools
APXS=$(top_builddir)/bin/apxs
APACHECTL=$(top_builddir)/bin/apachectl

#   additional defines, includes and libraries
MOD_CFLAGS=-Wall -g -O0
MOD_LDFLAGS=-L/usr/local/lib -lqrencode

#   the default target
all: local-shared-build

#   install the shared object file into Apache 
install: install-modules-yes
	rsync -r ../test/conf ../test/htdocs $(top_builddir)

#   cleanup
clean:
	-rm -f mod_qrg.o mod_qrg.lo mod_qrg.slo mod_qrg.la 

#   simple test
test: reload
	lynx -mime_header http://localhost/qrg

#   install and activate shared object by reloading Apache to
#   force a reload of the shared object file
reload: install restart

#   the general Apache start/restart/stop
#   procedures
start:
	$(APACHECTL) start
restart:
	$(APACHECTL) restart
stop:
	$(APACHECTL) stop

