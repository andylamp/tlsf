ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = nio-tlsf.pc nio-tlsf.spec

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nio-tlsf.pc

AM_CFLAGS = -Wall

lib_LTLIBRARIES = libnio-tlsf.la

libnio_tlsf_la_LDFLAGS = -version-info $(TLSF_CURRENT):$(TLSF_REVISION):$(TLSF_AGE)
libnio_tlsf_la_SOURCES = tlsf.c

include_HEADERS = tlsf.h

noinst_PROGRAMS = example tlsfbench

example_SOURCES = example.c $(HEADERS)
example_LDADD = $(top_builddir)/libnio-tlsf.la

tlsfbench_SOURCES = tlsf_bench.c $(HEADERS)
tlsfbench_LDADD = $(top_builddir)/libnio-tlsf.la
