all: teletext tvctl

teletext: main.o hamming.o

tvctl: tvctl.o

include Makefile.include
