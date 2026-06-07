CONTIKI_PROJECT = udp-client udp-server
PROJECT_SOURCEFILES += ota-metadata.c new-firmware-data.c
all: $(CONTIKI_PROJECT)

COFFEE_FILES = 1

CONTIKI=../..
include $(CONTIKI)/Makefile.include
