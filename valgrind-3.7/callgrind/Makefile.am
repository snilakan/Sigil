# /*
#    This file is part of Sigil, a tool for call graph profiling programs.

#    Copyright (C) 2012, [Author Name], Drexel University

#    This tool is derived from and contains code from Callgrind
#    Copyright (C) 2002-2011, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

#    This tool is also derived from and contains code from Cachegrind
#    Copyright (C) 2002-2011 Nicholas Nethercote (njn@valgrind.org)

#    This program is free software; you can redistribute it and/or
#    modify it under the terms of the GNU General Public License as
#    published by the Free Software Foundation; either version 2 of the
#    License, or (at your option) any later version.

#    This program is distributed in the hope that it will be useful, but
#    WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#    General Public License for more details.

#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
#    02111-1307, USA.

#    The GNU General Public License is contained in the file COPYING.
# */

include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST =  \
	docs/callgrind_annotate-manpage.xml \
	docs/callgrind_control-manpage.xml \
	docs/cl-manual.xml \
	docs/cl-format.xml

#----------------------------------------------------------------------------
# Headers, etc
#----------------------------------------------------------------------------

pkginclude_HEADERS = callgrind.h

bin_SCRIPTS = \
	callgrind_annotate \
	callgrind_control

noinst_HEADERS = \
	costs.h \
	events.h \
	global.h

#----------------------------------------------------------------------------
# callgrind-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = callgrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += callgrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

CALLGRIND_SOURCES_COMMON = \
	bb.c \
	bbcc.c \
	callstack.c \
	clo.c \
	context.c \
	costs.c \
	debug.c \
	dump.c \
	events.c \
	fn.c \
	jumps.c \
	main.c \
	sim.c \
	threads.c \
	../cachegrind/cg-arch.c \
	../cachegrind/cg-x86-amd64.c \
	../cachegrind/cg-ppc32.c \
	../cachegrind/cg-ppc64.c \
	../cachegrind/cg-arm.c   \
	../cachegrind/cg-s390x.c

CALLGRIND_CFLAGS_COMMON = -I$(top_srcdir)/cachegrind

callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(CALLGRIND_SOURCES_COMMON)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) $(CALLGRIND_CFLAGS_COMMON)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(callgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(CALLGRIND_SOURCES_COMMON)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) $(CALLGRIND_CFLAGS_COMMON)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(callgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif

