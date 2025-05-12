#ifndef SDR_UI_H
#define SDR_UI_H

#include <stdint.h>

#define VER_STR "s/zbitx K7IHZ expt"

// maximum sem_count in write_console_semantic()
#define MAX_CONSOLE_LINE_STYLES 8

#define EXT_PTT 26 //ADDED BY KF7YDU, solder lead wire to J17, which ties to pin 32.

#ifndef MAX_BINS
#define MAX_BINS 2048 // defined authoritatively in sdr.h
#endif

extern int ext_ptt_enable;
extern int display_freq;
extern int spectrum_plot[];

// A mixed bag of named styles used in various places in various UIs.
typedef enum {
	// semantic styles (only for the console so far):
	// STYLE_LOG must come first, because it's 0, the default,
	// and we use memset to initalize the console
	STYLE_LOG = 0,
	STYLE_MYCALL,
	STYLE_CALLER,
	STYLE_CALLEE,
	STYLE_GRID,
	STYLE_TIME,
	STYLE_SNR,
	STYLE_FREQ,
	// mode-specific semantics
	STYLE_FT8_RX,
	STYLE_FT8_TX,
	STYLE_FT8_QUEUED,
	STYLE_FT8_REPLY,
	STYLE_CW_RX,
	STYLE_CW_TX,
	STYLE_FLDIGI_RX,
	STYLE_FLDIGI_TX,
	STYLE_TELNET,

	// non-semantic styles, for other fields and UI elements
	STYLE_FIELD_LABEL,
	STYLE_FIELD_VALUE,
	STYLE_LARGE_FIELD,
	STYLE_LARGE_VALUE,
	STYLE_SMALL,
	STYLE_SMALL_FIELD_VALUE,
	STYLE_BLACK
}  sbitx_style;

/*	At first glance this may look silly: not the simplest way to style the "console".
	But this is an experiment in reusable UI design. Each instance of this struct can be applied
	to a span within the _entire_ body of text, even if the text is editable (to an extent),
	even if lines can be much longer than what we have in our "console".
	It's possible to save all text to a file, along with a vector of these structs in another file,
	and "replay" it with styling later: no need for some ad-hoc markup language.
	So a saved console session could just about make the logbook redundant: you could
	reconstruct the logbook from the session log, if you needed to. Time will tell whether
	that's useful, or just a waste of space to keep console logs around for too long.

	Text is meaningful to humans; metadata is kept separate. This is a better way to
	tag text for use by remote UIs that may present the information in a different way.

	The struct is 64 bits on purpose: it packs well in memory (most computers are 64-bit),
	and a memory image of a vector of these structs is meant to be portable to all
	little-endian machines.

	Too bad `semantic` is so short, but it's hard to imagine shortening any of the
	other fields (for the general use case outside this UI).
*/
typedef struct {
	uint32_t start_row : 32;
	uint16_t start_column : 16;
	uint8_t length : 8;
	uint8_t semantic : 8; // used directly as style in this UI
} text_span_semantic;

time_t time_sbitx();
void setup();
void loop();
void display();
void redraw();
void key_pressed(char c);
int set_field(const char *id, const char *value);
int field_set(const char *label, const char *new_value);
int get_field_value(const char *id, char *value);
int get_field_meta(const char *id, int *min, int *max, int *step);
int get_field_value_by_label(const char *label, char *value);
const char *field_str(const char *label);
int field_int(char *label);
void write_console(sbitx_style style, const char *text);
// write plain text, with semantically-tagged spans that imply styling
void write_console_semantic(const char *text, const text_span_semantic *sem, int sem_count);
int get_console_text(char *buf, int max, int from_char, sbitx_style filter);
int get_console_text_spans(text_span_semantic *buf, int max, int from_char, sbitx_style filter);
int console_current_length(sbitx_style filter, int last_line);
int console_current_spans_length(sbitx_style filter, int last_line);
time_t console_last_time();
int console_last_line(); // the line into which incoming text will go / is going
int web_get_console(char *buff, int max);
int is_in_tx();
void abort_tx();
void remote_execute(const char *command);
int remote_update_field(int i, char *text);
void web_get_spectrum(char *buff);
void save_user_settings(int forced);
int remote_audio_output(int16_t *samples);
void enter_qso();
void call_wipe();
void update_log_ed();
void write_call_log();
int macro_load(const char *filename, char *output);
int macro_exec(int key, char *dest);
void macro_label(int fn_key, char *label);
void macro_list(char *output);
void macro_get_keys(char *output);
int get_spectrum_8bit(uint8_t *buf, int max);

#endif // SDR_UI_H
