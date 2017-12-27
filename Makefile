CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi

LDFLAGS+=-L$(SDKSTAGE)/opt/vc/lib/ -lbcm_host

INCLUDES+=-I$(SDKSTAGE)/opt/vc/include/ -I./

TELETEXT_OFILES=teletext.o buffer.o hamming.o demo.o

CEA608_OFILES=cea608.o cea608buffer.o

all: tvctl teletext cea608

teletext: $(TELETEXT_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(TELETEXT_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

cea608: $(CEA608_OFILES)
	$(CC) -o $@ -Wl,--whole-archive $(CEA608_OFILES) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

%.o: %.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

clean:
	rm -f *.o teletext cea608 tvctl
