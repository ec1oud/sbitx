TARGET = sbitx
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(wildcard src/*.h)
CFLAGS = -g `pkg-config --cflags gtk+-3.0`
LIBS = -lwiringPi -lasound -lm -lfftw3 -lfftw3f -pthread -lncurses -lsqlite3 -lnsl -lrt -lixp src/ft8_lib/libft8.a `pkg-config --libs gtk+-3.0`
CC = gcc
LINK = gcc
STRIP = strip

$(TARGET): $(OBJECTS)
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(LIBPATH) $(LIBS)

.c.o: $(HEADERS)
	$(CC) -c $(CFLAGS) $(DEBUGFLAGS) $(INCPATH) -o $@ $<

clean:
	-rm -f $(OBJECTS)
	-rm -f *~ core *.core
	-rm -f $(TARGET)

test:
	echo $(OBJECTS)
