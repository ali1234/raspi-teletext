# The dispmanx renderer needs the legacy firmware display stack (/opt/vc).
# Without it, as on Raspberry Pi 4 with the vc4-kms-v3d driver, the
# programs are built against DRM/KMS instead. Override with RENDER=kms or
# RENDER=dispmanx.
ifeq ($(wildcard $(SDKSTAGE)/opt/vc/include/bcm_host.h),)
RENDER ?= kms
else
RENDER ?= dispmanx
endif

ifeq ($(RENDER),kms)
CFLAGS+=-DRENDER_KMS -Wall -g -pipe $(shell pkg-config --cflags libdrm)
LDFLAGS+=$(shell pkg-config --libs libdrm) -pthread
RENDER_OFILES=render_kms.o
else
CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi
LDFLAGS+=-L$(SDKSTAGE)/opt/vc/lib/ -lbcm_host -pthread
INCLUDES+=-I$(SDKSTAGE)/opt/vc/include/
RENDER_OFILES=render.o
endif

INCLUDES+=-I./

TELETEXT_OFILES=teletext.o $(RENDER_OFILES) buffer.o hamming.o demo.o

CEA608_OFILES=cea608.o $(RENDER_OFILES) cea608buffer.o

VIDEODAT_OFILES=videodat.o $(RENDER_OFILES) vdbuffer.o vdline.o vdpacket.o vddemo.o

TTVD_OFILES=ttvd.o $(RENDER_OFILES) buffer.o hamming.o demo.o vdbuffer.o vdline.o vdpacket.o vddemo.o

all: tvctl teletext cea608 videodat ttvd vdenc vdvbi

teletext: $(TELETEXT_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(TELETEXT_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

cea608: $(CEA608_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(CEA608_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

videodat: $(VIDEODAT_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(VIDEODAT_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic -lm

ttvd: $(TTVD_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(TTVD_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic -lm

# vdenc and vdvbi don't need the display, so they also build on a PC.
vdenc: vdenc.o vdpacket.o
	$(CC) -o $@ vdenc.o vdpacket.o

vdvbi: vdvbi.o vdline.o
	$(CC) -o $@ vdvbi.o vdline.o -lm

%.o: %.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

clean:
	rm -f *.o teletext cea608 tvctl videodat ttvd vdenc vdvbi
