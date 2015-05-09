#include <stdarg.h>
#include <time.h>
#include <unistd.h>

//#include "base.h"
#include "format.h"

#define trace_type(l) (((l) >> (sizeof(size_t) * 8 - 2)) & 0x1L)
#define trace_length(l) ((l) & ~(0x3L << (sizeof(size_t) * 8 - 2)))

// TODO the limited size of buffer may lead to crash
// TODO C99 limits trace arguments to 63 ((127 - 1) / 2)
void trace(int fd, ...)
{
	va_list buffers;
	size_t length;
	int64_t integer;
	char *string;

	char buffer[1024], *start = buffer;

#if defined(OS_BSD)
	static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	struct tm info;
	time_t timestamp = time(0);
	gmtime_r(&timestamp, &info);

	// yyyy MON dd hh:mm:ss
	*start++ = '[';
	start = format_uint(start, info.tm_mday, 10, 2, '0');
	*start++ = ' ';
	start = format_bytes(start, months[info.tm_mon], 3);
	*start++ = ' ';
	start = format_uint(start, info.tm_year + 1900, 10, 4, '0');
	*start++ = ' ';
	start = format_uint(start, info.tm_hour, 10, 2, '0');
	*start++ = ':';
	start = format_uint(start, info.tm_min, 10, 2, '0');
	*start++ = ':';
	start = format_uint(start, info.tm_sec, 10, 2, '0');
	*start++ = ']';
	*start++ = ' ';
#endif

	// Handle each buffer passed as an argument
	va_start(buffers, fd);
	while (length = va_arg(buffers, size_t))
	{
		if (trace_type(length)) // string
		{
			string = va_arg(buffers, char *);
			start = format_bytes(start, string, trace_length(length));
		}
		else // integer
		{
			integer = va_arg(buffers, int64_t);
			start = format_int(start, integer, 10);
		}
	}
	va_end(buffers);

	*start++ = '\n';

	write(fd, buffer, start - buffer);
}
