TARGET = sbitxd
SOURCES = $(wildcard src/*.c)
OWNER ?= $(TARGET)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
STATEDIR ?= /var/lib/$(OWNER)
SHAREDIR ?= $(PREFIX)/share/$(OWNER)
OBJECTS = $(SOURCES:.c=.o)
FFTOBJ = ft8_lib/.build/fft/kiss_fft.o ft8_lib/.build/fft/kiss_fftr.o
HEADERS = $(wildcard src/*.h)
CFLAGS = -I.
LIBS = -lwiringPi -lasound -lm -lfftw3 -lfftw3f -pthread -lsqlite3 -lnsl -lrt -lssl -lcrypto -lsystemd -lixp ft8_lib/libft8.a
ifdef SBITX_UNUSED
## remove and print unused code
CFLAGS += -ffunction-sections -fdata-sections
LIBS += -Wl,--gc-sections,--print-gc-sections
endif
ifdef SBITX_DEBUG
CFLAGS += -ggdb3 -fsanitize=address
LIBS += -fsanitize=address
endif
CC = gcc
LINK = gcc
STRIP = strip
# Define Mongoose SSL flags: ensure OpenSSL is properly enabled
MONGOOSE_FLAGS = -DMG_ENABLE_OPENSSL=1 -DMG_ENABLE_MBEDTLS=0 -DMG_TLS=MG_TLS_OPENSSL -DMG_ENABLE_SSI=0 -DMG_ENABLE_IPV6=0

$(TARGET): create_configure.h $(OBJECTS) ft8_lib/libft8.a
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(FFTOBJ) $(LIBPATH) $(LIBS)
	sudo setcap CAP_SYS_TIME+ep $(TARGET) # Provide capability to adjust the local system time -W2JON
	sudo setcap CAP_NET_BIND_SERVICE=+eip $(TARGET) # Allow 9p to listen on port 564

src/mongoose.o: src/mongoose.c
	$(CC) -c $(CFLAGS) $(DEBUGFLAGS) $(INCPATH) $(MONGOOSE_FLAGS) -o $@ $<

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUGFLAGS) $(INCPATH) -o $@ $<

create_configure.h:
	$(shell echo "#define STATEDIR \"$(STATEDIR)\"" > configure.h) 
	$(shell echo "#define SHAREDIR \"$(SHAREDIR)\"" >> configure.h) 

ft8_lib/libft8.a:
ifdef SBITX_DEBUG
	$(MAKE) FT8_DEBUG=1 -C ft8_lib
else
	$(MAKE) -C ft8_lib
endif

clean:
	-rm -f configure.h
	-rm -f $(OBJECTS)
	-rm -f *~ core *.core
	-rm -f $(TARGET)
	$(MAKE) -C ft8_lib clean

adduser:
	-adduser --system --group --home $(DESTDIR)/$(STATEDIR) --disabled-password $(OWNER)
	-adduser $(OWNER) audio
	-adduser $(OWNER) gpio

install: adduser
	install -D --mode=755 $(TARGET) $(DESTDIR)/$(BINDIR)/$(TARGET)
	sudo setcap CAP_SYS_TIME+ep $(DESTDIR)/$(BINDIR)/$(TARGET)
	sudo setcap CAP_NET_BIND_SERVICE=+eip $(DESTDIR)/$(BINDIR)/$(TARGET)
	install -d --owner=$(OWNER) --group=$(OWNER) $(DESTDIR)/$(SHAREDIR)/web
	install -m 644 --owner=$(OWNER) --group=$(OWNER) web/* $(DESTDIR)/$(SHAREDIR)/web ||:
	install -d --owner=$(OWNER) --group=$(OWNER) $(DESTDIR)/$(STATEDIR)
	install -m 644 --owner=$(OWNER) --group=$(OWNER) data/default_hw_settings.ini $(DESTDIR)/$(STATEDIR)
	install -m 644 --owner=$(OWNER) --group=$(OWNER) data/default_settings.ini $(DESTDIR)/$(STATEDIR)
	install -d $(DESTDIR)/$(PREFIX)/lib/systemd/system/
	install -m 644 systemd/sbitxd.service $(DESTDIR)/$(PREFIX)/lib/systemd/system
	ln -sf /var/lib/sbitxd/grids.txt /usr/local/share/sbitxd/web/grids.txt
ifeq ("$(wildcard $(DESTDIR)/$(STATEDIR)/sbitx.db)","")
	$(shell sqlite3 $(DESTDIR)/$(STATEDIR)/sbitx.db < data/create_db.sql)
endif

uninstall:
	rm -f $(DESTDIR)/$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)/$(SHAREDIR)
	rm -f $(DESTDIR)/$(STATEDIR)/default_hw_settings.ini
	rm -f $(DESTDIR)/$(STATEDIR)/default_settings.ini
	rm -f $(DESTDIR)/$(PREFIX)/lib/systemd/system/sbitxd.service

.PHONY: adduser create_configure.h clean install uninstall
