BUILD_DIR = .build

FT8_SRC  = $(wildcard ft8/*.c)
FT8_OBJ  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(FT8_SRC))

COMMON_SRC = $(wildcard common/*.c)
COMMON_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SRC))

FFT_SRC  = $(wildcard fft/*.c)
FFT_OBJ  = $(patsubst %.c,$(BUILD_DIR)/%.o,$(FFT_SRC))

TARGETS  = gen_ft8 decode_ft8 test_ft8 libft8.a

ifdef FT8_DEBUG
CFLAGS   = -fsanitize=address -ggdb3 -DHAVE_STPCPY -I.
LDFLAGS  = -fsanitize=address -lm
else
CFLAGS   = -O3 -DHAVE_STPCPY -I.
LDFLAGS  = -lm
endif

# Optionally, use Portaudio for live audio input
# Portaudio is a C++ library, so then you need to build with clang++ or g++:
# it's up to you to set CC accordingly!
# Then -std=c11 is useful.
ifdef PORTAUDIO_PREFIX
CFLAGS += -std=c11 -DUSE_PORTAUDIO -I$(PORTAUDIO_PREFIX)/include
LDFLAGS  += -lportaudio -L$(PORTAUDIO_PREFIX)/lib
endif

.PHONY: all clean run_tests install

all: $(TARGETS) libft8.a

clean:
	rm -rf $(BUILD_DIR) $(TARGETS) 

run_tests: test_ft8
	@./test_ft8

install:
	install libft8.a /usr/lib/libft8.a

libft8.a: $(FT8_OBJ) $(COMMON_OBJ)
	$(AR) rc libft8.a $(FT8_OBJ) $(COMMON_OBJ)

gen_ft8: $(BUILD_DIR)/gen_ft8.o libft8.a $(FFT_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

decode_ft8: $(BUILD_DIR)/decode_ft8.o libft8.a $(FFT_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

test_ft8: $(BUILD_DIR)/test/test.o libft8.a $(FFT_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $^
