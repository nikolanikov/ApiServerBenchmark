#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>

#include "base.h"
#include "format.h"
#include "stream.h"
#include "http.h"

// TODO: ? url_decode NUL terminator 

// Numbers corresponding to each hexadecimal digit written in ASCII (0xff = invalid value)
static const unsigned char hex2int[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff,   10,   11,   12,   13,   14,   15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff,   10,   11,   12,   13,   14,   15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// Decodes length bytes of src to dest. dest must be at least length bytes long. length must be positive.
// On success returns the length of the decoded string. On error returns 0.
size_t url_decode(const char *src, char *restrict dest, size_t length)
{
	size_t s, d;
	unsigned char high, low;

	for(s = 0, d = 0; s < length; ++s, ++d)
	{
		if (src[s] == '%')
		{
			// Check array boundaries
			if ((length - s) < 2) return 0;

			high = hex2int[(size_t)src[s + 1]];
			if (high == 0xff) return 0;
			low = hex2int[(size_t)src[s + 2]];
			if (high == 0xff) return 0;

			dest[d] = (high << 4) | low;
			s += 2;
		}
		else dest[d] = src[s];
	}

	return d;
}

// Generate table of allowed characters in URI.
// http://www.ietf.org/rfc/rfc2396.txt
/*
#include <ctype.h>
static void uri_table(void)
{
	unsigned char n = 0;
	unsigned char table[32] = {0};
	unsigned char rem;
	size_t index = 0;

	do
	{
		rem = (n & 0x7);

		// Set bit if the character is allowed.
		if (isalnum(n)) table[index] |= (1 << rem);
		else switch (n)
		{
		case '-':
		case '_':
		case '.':
		case '!':
		case '~':
		case '*':
		case '\'':
		case '(':
		case ')':
			table[index] |= (1 << rem);
		}

		if (rem == 7) index += 1;
		n += 1;
	} while (index < sizeof(table));

	unsigned char print[4] = "\\x\0\0";
	for(index = 0; index < sizeof(table); ++index)
	{
		format_hex(print + 2, table + index, 1);
		write(1, print, 4);
	}
	write(1, "\n", 1);
}
*/

static inline bool allowed(unsigned char character)
{
	// The bit table of the allowed characters is generated with uri_table().
	// The original table is substituted with one that doesn't encode some characters as a workaround for Dropbox API bug.
	//static const unsigned char table[32] = "\x00\x00\x00\x00\x82\x67\xff\x03\xfe\xff\xff\x87\xfe\xff\xff\x47\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
	static const unsigned char table[32] = "\x00\x00\x00\x00\xc2\xe7\xff\xa3\xfe\xff\xff\x87\xfe\xff\xff\x47\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
	return (table[character >> 3] & (1 << (character & 0x7)));
}

// TODO improve this
struct string *restrict uri_encode(const char *restrict source, size_t size)
{
	size_t index;

	// Calculate length of the encoded URI.
	size_t length = size;
	for(index = 0; index < size; ++index)
		if (!allowed(source[index]))
			length += 2;

	struct string *encoded = malloc(sizeof(struct string) + length + 1);
	if (!encoded) return 0;
	encoded->data = (char *)(encoded + 1);
	encoded->length = length;

	// Generate encoded URI.
	length = 0;
	for(index = 0; index < size; ++index)
	{
		if (allowed(source[index])) encoded->data[length++] = source[index];
		else
		{
			encoded->data[length++] = '%';
			format_hex(encoded->data + length, source + index, 1);
			length += 2;
		}
	}

	return encoded;
}

// Returns the specified time in the format specified by RFC 1123:
// Sun, 06 Nov 1994 08:49:37 GMT
void http_date(char buffer[HTTP_DATE_LENGTH + 1], time_t timestamp)
{
	static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	struct tm info;

#if !defined(OS_WINDOWS)
	gmtime_r(&timestamp, &info);
#else
	//mingw gmtime is thread safe because windows time function is thread safe
	struct tm *info_p = gmtime(&timestamp);
	info = *info_p;
#endif
	
	// Generate date string. Assume sprintf will not fail
	sprintf(buffer, "%3s, %02u %3s %04u %02u:%02u:%02u GMT",
		days[info.tm_wday],
		info.tm_mday,
		months[info.tm_mon],
		info.tm_year + 1900,
		info.tm_hour,
		info.tm_min,
		info.tm_sec
	);
}

// Workaround buggy browsers that report no error on premature close().
// It appears that Mozilla are taking actions to solve the issue: https://bugzilla.mozilla.org/show_bug.cgi?id=237623
// Make the socket send RST on close(). This must be overwritten before a successful close().
// http://alas.matf.bg.ac.rs/manuals/lspe/snode=105.html
// http://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
// http://developerweb.net/viewtopic.php?id=2982
void http_open(int sock)
{
	struct linger linger = {.l_onoff = 1};
#if !defined(OS_WINDOWS)
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger)) < 0)
#else
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) < 0)
#endif
	{
		// TODO is this possible?
	}
}
void http_close(int sock)
{
	struct linger linger = {.l_onoff = 0};
#if !defined(OS_WINDOWS)
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger)) < 0)
#else
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger)) < 0)
#endif
	{
		// TODO is this possible?
	}

#if !defined(OS_WINDOWS)
	close(sock);
#else
	CLOSE(sock);
#endif
}

/*unsigned http_error(int code)
{
	switch (code)
	{
	case ERROR_AGAIN:
	case ERROR_MEMORY:
		return ServiceUnavailable;
	case ERROR_ACCESS:
		return Forbidden;
	case ERROR_MISSING:
		return NotFound;
	case ERROR_UNSUPPORTED:
		return NotImplemented;

	case ERROR_INPUT:
	case ERROR_READ:
	case ERROR_WRITE:
	case ERROR_CANCEL:
	case ERROR_EXIST:

	case ERROR_EVFS:
	case ERROR:
		return InternalServerError;
	}
}*/

/*void tes(const char *restrict data, size_t size)
{
	struct string *value;
	char buffer[256];

	value = uri_encode(data, size);
	size_t len = url_decode(value->data, buffer, value->length);
	buffer[len] = 0;
	printf("%s\n", data);
	printf("%s\n", value->data);
	printf("%s\n", buffer);
	if ((size == len) && !memcmp(data, buffer, size)) printf("OK\n\n");
	else printf("Fail %d\n\n", (int)len);
	free(value);
}
int main(void)
{
	#define E(s) tes((s), (sizeof(s) - 1))

	E("kuche");
	E("this is some URI !@^&!%#$&*(#)!@");
	E("encode...\n decode...");
	E("&*#@EHUDIOPEW0_@#j3wr78euidEfoiewfj");
	E("?a=3&b=blqh!");

	#undef E

	return 0;
}*/
