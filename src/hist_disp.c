#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "sdr_ui.h"
#include "logbook.h"
#include "hist_disp.h"

bool isLetter(char c) {
    return c >= 'A' && c <= 'Z';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isValidGridId(char* gridId) {
	return strlen(gridId) == 4 &&
		isLetter(gridId[0]) && isLetter(gridId[1]) &&
        isDigit(gridId[2]) && isDigit(gridId[3]);
}

static FILE* onfFout;

void addGridToFile(char * gridId, int cnt) {
    if (isValidGridId(gridId)) {
		if (onfFout != NULL) {
				fwrite(gridId,1,4,onfFout);
        }
    }
}

void hd_createGridList() {
	onfFout = fopen("./web/grids.txt", "w");

	logbook_open();
	logbook_get_grids(addGridToFile);

	if (onfFout != NULL) {
		fwrite("\0\0", 1, 2, onfFout);
		fclose(onfFout);
	}
}

struct hd_message_struct {
	char signal_info[32];
	char m1[32], m2[32], m3[32], m4[32];
};

int hd_next_token(const char* src, int start, char* tok, int tok_max, char * sep) {
	tok[0] = 0;
	if (src == NULL || src[start] == 0)
		return -1;
	const char * p_sep;
	int n, p;
	int len = strlen(src);
	if (len > 0 && src[len-1] == '\n') {
		len--; // strip trailing newline
	}
	do {
		p_sep = strstr(src + start, sep);
		if (p_sep == NULL) {
			p_sep = src+len;
		}
		n = p_sep - (src + start);
		p = start;
		start = start + n + strlen(sep);
	} while (n == 0 && start < len);
	if (n > tok_max) return -2;
	memcpy(tok, src + p, n);
	tok[n] = 0;
	return p + n + strlen(sep);
}

int hd_message_parse(struct hd_message_struct* p_message, const char* raw_message) {
	int r = hd_next_token(raw_message, 0, p_message->signal_info, 32, "~ ");
	if (r < 0 ) return r;
	r = hd_next_token(raw_message, r, p_message->m1, 32, " ");
	if (r < 0) return r;
	r = hd_next_token(raw_message, r, p_message->m2, 32, " ");
	if (r < 0) return r;
	r = hd_next_token(raw_message, r, p_message->m3, 32, " ");
	if (r < 0) return r;
	r = hd_next_token(raw_message, r, p_message->m4, 32, " ");
	if (r < -1) return r;
	return 0;
}

int ff_lookup_style(char* id, int style, int style_default) {
	switch (style)
	{
	case STYLE_CALLER:
		return logbook_caller_exists(id) ? style_default : style;
		// return style; // test skipping log lookup
		break;

	case STYLE_GRID: {
		bool id_ok =
			(strlen(id) == 4 && strcmp(id,"RR73") &&
			isLetter(id[0]) && isLetter(id[1]) &&
        	isDigit(id[2]) && isDigit(id[3]));

			return (!id_ok || logbook_grid_exists(id)) ? style_default : style;
			//return (!id_ok) ? style_default : style; // test skipping log lookup
		}
		break;

	default:
		break;
	}
	return style;
}

char *ff_cs(char * markup, int style) {
	markup[0] = HD_MARKUP_CHAR;

	/* used to be 'A' + style, where style came from these:
	#define FONT_FIELD_LABEL 0
	#define FONT_FIELD_VALUE 1
	#define FONT_LARGE_FIELD 2
	#define FONT_LARGE_VALUE 3
	#define FONT_SMALL 4
	#define FONT_LOG 5
	#define FONT_FT8_RX 6
	#define FONT_FT8_TX 7
	#define FONT_SMALL_FIELD_VALUE 8
	#define FONT_CW_RX 9
	#define FONT_CW_TX 10
	#define FONT_FLDIGI_RX 11
	#define FONT_FLDIGI_TX 12
	#define FONT_TELNET 13
	#define FONT_FT8_QUEUED 14
	#define FONT_FT8_REPLY 15

	#define FF_MYCALL 16
	#define FF_CALLER 17
	#define FF_GRID 18
	#define FONT_BLACK 19
	*/

	switch (style) {
		// console styles
		case STYLE_LOG:
			markup[1] = 'A' + 5;
			break;
		case STYLE_MYCALL:
			markup[1] = 'A' + 16;
			break;
		case STYLE_CALLER:
			markup[1] = 'A' + 17;
			break;
		case STYLE_CALLEE:
			markup[1] = 'A' + 5;
			break;
		case STYLE_GRID:
			markup[1] = 'A' + 18;
			break;
		case STYLE_FT8_RX:
			markup[1] = 'A' + 6;
			break;
		case STYLE_FT8_TX:
			markup[1] = 'A' + 7;
			break;
		case STYLE_FT8_QUEUED:
			markup[1] = 'A' + 14;
			break;
		case STYLE_FT8_REPLY:
			markup[1] = 'A' + 15;
			break;
		case STYLE_CW_RX:
			markup[1] = 'A' + 9;
			break;
		case STYLE_CW_TX:
			markup[1] = 'A' + 10;
			break;
		case STYLE_FLDIGI_RX:
			markup[1] = 'A' + 11;
			break;
		case STYLE_FLDIGI_TX:
			markup[1] = 'A' + 12;
			break;
		case STYLE_TELNET:
			markup[1] = 'A' + 13;
			break;

		// field styles
		case STYLE_FIELD_LABEL:
			markup[1] = 'A' + 0;
			break;
		case STYLE_FIELD_VALUE:
			markup[1] = 'A' + 1;
			break;
		case STYLE_LARGE_FIELD:
			markup[1] = 'A' + 2;
			break;
		case STYLE_LARGE_VALUE:
			markup[1] = 'A' + 3;
			break;
		case STYLE_SMALL:
			markup[1] = 'A' + 4;
			break;
		case STYLE_SMALL_FIELD_VALUE:
			markup[1] = 'A' + 8;
			break;
		case STYLE_BLACK:
			markup[1] = 'A' + 19;
			break;
	}
	markup[2] = 0;
	return markup;
}

// convert from semantic style to old numeric line style font (as used on zbitx i2c display)
int old_style_font(int style) {
	switch (style) {
	case STYLE_FT8_RX:
		return 6; // FONT_FT8_RX
	case STYLE_FT8_TX:
		return 7; // FONT_FT8_TX
	case STYLE_FT8_QUEUED:
		return 14; // FONT_FT8_QUEUED
	case STYLE_FT8_REPLY:
		return 15; // FONT_FT8_REPLY
	case STYLE_CW_RX:
		return 9; // FONT_CW_RX
	case STYLE_CW_TX:
		return 10; // FONT_CW_TX
	case STYLE_FLDIGI_RX:
		return 11; // FONT_FLDIGI_RX
	case STYLE_FLDIGI_TX:
		return 12; // FONT_FLDIGI_TX
	case STYLE_TELNET:
		return 13; // FONT_TELNET
	// The rest of the FONT_ defines are not meant for the console, so we don't need them
	default: // or STYLE_LOG
		return 5; // FONT_LOG
	}
}

char* ff_style(char* decorated, struct hd_message_struct *pms, int style_default, int style1, int style2, int style3, int style4) {
	char markup[3];
	*decorated = 0;

	strcat(decorated, ff_cs(markup, style_default));
	strcat(decorated, pms->signal_info);
	strcat(decorated, "~ ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m1, style1, style_default)));
	strcat(decorated, pms->m1);
	strcat(decorated, " ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m2, style2, style_default)));
	strcat(decorated, pms->m2);
	strcat(decorated, " ");

	strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m3, style3, style_default)));
	strcat(decorated, pms->m3);

	if (style4) {
		strcat(decorated, " ");
		strcat(decorated, ff_cs(markup, ff_lookup_style(pms->m4, style4, style_default)));
		strcat(decorated, pms->m4);
	}
	strcat(decorated, "\n");

}

int hd_length_no_decoration( char * decorated) {
	int len = 0;
	while(*decorated)
		if (*decorated++ == HD_MARKUP_CHAR)
			len--;
		else
			len++;
	return len < 0 ? 0 : len;
}


void hd_strip_decoration(char * ft8_message, char * decorated) {
	while(*decorated) {
		if (*decorated == HD_MARKUP_CHAR && *(decorated+1) != 0) {
			decorated += 2;
		} else if (*decorated == '<' || *decorated == '>') {
			decorated += 1;
		} else {
			*ft8_message++ = *decorated++;
		}
	}
	*ft8_message = 0;
}

int hd_decorate(int style, const char * message, char * decorated) {

	switch (style) {
	case STYLE_FT8_RX:
	case STYLE_FT8_TX:
	case STYLE_FT8_QUEUED:
	case STYLE_FT8_REPLY:
		{
		decorated[0] = 0;
			struct hd_message_struct fms;
			const char* my_callsign = field_str("MYCALLSIGN");
			int res = hd_message_parse(&fms, message);
			if (res == 0) {
				if (!strcmp(fms.m1, "CQ")) {
					if (fms.m4[0] == 0) { // CQ caller grid
						ff_style(decorated, &fms, style, STYLE_LOG, STYLE_CALLER, STYLE_GRID, 0);
					}
					else { // CQ DX caller grid
						ff_style(decorated, &fms, style, STYLE_LOG, STYLE_LOG, STYLE_CALLER, STYLE_GRID);
					}
				} else if (!strcmp(fms.m1, my_callsign))
				{ // mycall caller grid|report
					ff_style(decorated, &fms, style, STYLE_MYCALL, STYLE_CALLER, STYLE_GRID, 0);
				} else if (!strcmp(fms.m2, my_callsign))
				{ // caller mycall grid|report
					ff_style(decorated, &fms, style, STYLE_CALLER, STYLE_MYCALL, STYLE_GRID, 0);
				} else
				{ // other caller grid|report
					ff_style(decorated, &fms, style, style, STYLE_CALLER, STYLE_GRID, 0);
				}
			}
			return res;
		}
		break;
	default:
		strcpy(decorated, message);
	}
	return 0;
}
