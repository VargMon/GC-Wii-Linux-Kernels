#
# Makefile for the DECstation prom monitor library routines
# under Linux.
#

lib-y			+= init.o memory.o cmdline.o identify.o console.o

lib-$(CONFIG_32BIT)	+= locore.o
lib-$(CONFIG_64BIT)	+= call_o32.o

EXTRA_AFLAGS := $(CFLAGS)
