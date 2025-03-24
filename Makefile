TARGET = sbitx
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)
FFTOBJ = ft8_lib/.build/fft/kiss_fft.o ft8_lib/.build/fft/kiss_fftr.o
HEADERS = $(wildcard src/*.h)
CFLAGS = `pkg-config --cflags gtk+-3.0` -I.
LIBS = -lwiringPi -lasound -lm -lfftw3 -lfftw3f -pthread -lncurses -lsqlite3 -lnsl -lrt -lixp ft8_lib/libft8.a `pkg-config --libs gtk+-3.0`
ifdef SBITX_DEBUG
CFLAGS += -ggdb3 -fsanitize=address
LIBS += -fsanitize=address
endif
CC = gcc
LINK = gcc
STRIP = strip

$(TARGET): $(OBJECTS) ft8_lib/libft8.a
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(FFTOBJ) $(LIBPATH) $(LIBS)

.c.o: $(HEADERS)
	$(CC) -c $(CFLAGS) $(DEBUGFLAGS) $(INCPATH) -o $@ $<

ft8_lib/libft8.a:
ifdef SBITX_DEBUG
	$(MAKE) FT8_DEBUG=1 -C ft8_lib
else
	$(MAKE) -C ft8_lib
endif

clean:
	-rm -f $(OBJECTS)
	-rm -f *~ core *.core
	-rm -f $(TARGET)

test:
	echo $(OBJECTS)
