#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base.h"
#include "format.h"
#include "json.h"
#include "stream.h"
#include "http.h"
#include "http_parse.h"

#define URI_LENGTH_MAX 16384 /* 16KiB */

// http_parse* functions return 0 on success and HTTP status code on error

static struct string *string_alloc(const char *data, size_t length)
{
    struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
    if (!result) return 0;
    result->data = (char *)(result + 1);
    result->length = length;
    if (data) memcpy(result->data, data, length);
    result->data[length] = 0;
    return result;
}

static int http_parse_uri_path(struct http_request *restrict request, size_t path_pos)
{
	size_t length = request->URI.length - path_pos, path_length;
	//if (length <= 1) return NotFound; // TODO this is commented because it looks useless; make sure it's not necessary

	const char *path = request->URI.data + path_pos;
	const char *query_start = strchr(path, '?');

	if (query_start)
	{
		path_length = query_start - path;
		size_t query_length = length - path_length - 1;

		// Decode query
		struct string json_raw;
		json_raw.data = malloc(sizeof(char) * (query_length + 1));
		if (!json_raw.data) return ServiceUnavailable;

		json_raw.length = url_decode(query_start + 1, json_raw.data, query_length);
		if (!json_raw.length)
		{
			free(json_raw.data);
			return BadRequest;
		}
		json_raw.data[json_raw.length] = 0;

		// Parse query
		request->query = json_parse(&json_raw);
		free(json_raw.data);
		if (!request->query) return BadRequest; // TODO: this could be either BadRequest or InternalServerError because of json_parse
	}
	else
	{
		path_length = length;
		request->query = 0;
	}

	// Decode path
	request->path.data = malloc(sizeof(char) * (path_length + 1));
	if (!request->path.data)
	{
		json_free(request->query);
		return ServiceUnavailable;
	}
	request->path.length = url_decode(path, request->path.data, path_length);
	if (!request->path.length)
	{
		json_free(request->query);
		free(request->path.data);
		return BadRequest;
	}
	request->path.data[request->path.length] = 0;

	return 0;
}

// Given a URI, this function sets protocol, port, path, query (and, possibly, the host header).
// The argument passed must be initialized with 0.
// On success returns 0. On error returns the HTTP status code of the error.
int http_parse_uri(struct http_request *restrict request)
{
	size_t length = request->URI.length;

	if (!length) // Empty URI
	{
		request->path.length = 1;
		request->path.data = dupalloc("/", request->path.length);
		if (!request->path.data) return ServiceUnavailable;
		request->query = 0;
		return 0;
	}
	else if (request->URI.data[0] == '/') // URI contains only path
	{
		return http_parse_uri_path(request, 0);
	}
	else
	{
		const char *host, *path;
		size_t host_length;

		request->path.data = 0;
		request->query = 0;

		// Parse URI scheme.
		size_t index;
		const struct string http = string("http"), separator = string("://");
		if ((length < (http.length + separator.length)) || memcmp(request->URI.data, http.data, http.length)) return BadRequest;
		if (request->URI.data[http.length] == 's')
		{
			index = http.length + 1;
			request->protocol = PROTOCOL_HTTPS;
		}
		else
		{
			index = http.length;
			request->protocol = PROTOCOL_HTTP;
		}
		if (((length - index) < separator.length) || memcmp(request->URI.data + index, separator.data, separator.length)) return BadRequest;
		index += separator.length;

		host = request->URI.data + index;
		path = strchr(host, '/');
		if (!path) return BadRequest;

		// Set port.
		char *sep = strchr(host, ':');
		if (sep)
		{
			errno = 0;
			request->port = (unsigned)strtol(sep + 1, 0, 10);
			if (!request->port && errno) return BadRequest;
			host_length = sep - host;
		}
		else host_length = path - host;

		// Add host header.
		// WARNING: The code below assumes that request->headers contains key "host".
		struct string name = string("host"), *value = string_alloc(host, host_length);
		if (!value) return ServiceUnavailable;
		void *old;
		dict_set(&request->headers, &name, value, &old);
		free(old);

		return http_parse_uri_path(request, path - request->URI.data);
	}
}

// TODO: make sure all possibilities for the format are handled
// Parses header field and allocates memory for it. Returns 0 on success or error HTTP status code on error
static int http_parse_header_field(struct dict *restrict headers, const char *buffer, unsigned length)
{
	size_t i;
	for(i = 0; i < length; ++i)
	{
		if (buffer[i] == ':') // If this is the end of field name
		{
			struct string name, *value;
			name.length = i;

			// Ignore initial whitespace characters
			for(++i; i < length; ++i)
				if ((buffer[i] != ' ') && (buffer[i] != '\t'))
					break;

			// Allocate memory for the header field. Set field value.
			name.data = malloc(sizeof(char) * (name.length + 1));
			if (!name.data) return ERROR_MEMORY;
			value = string_alloc(buffer + i, length - i);
			if (!value)
			{
				free(name.data);
				return ERROR_MEMORY;
			}

			// Generate lowercase version of header name.
			for(i = 0; i < name.length; ++i)
				name.data[i] = tolower(buffer[i]);
			name.data[name.length] = 0;

			// Add header.
			int status = dict_add(headers, &name, value);
			free(name.data);
			if (!status) return 0; // success
			free(value);
			if (status == ERROR_EXIST) return 0; // header already added
			else return status; // memory error
		}
	}

	return ERROR_INPUT;
}

int http_parse_header(struct dict *restrict header, struct stream *restrict stream)
{
	size_t index = 0, start = 0, length;
	struct string buffer = {.length = 0};
	int status; // Used for storing error code

	if (!dict_init(header, DICT_SIZE_BASE)) return ERROR_MEMORY;

	while (1)
	{
		if (index == buffer.length)
		{
			// Read more data while keeping everything from the start of this header field
			index -= start;
			start = 0;
			if (stream_read(stream, &buffer, index + 1))
			{
				status = -1;
				break;
			}
		}

		if (buffer.data[index] == '\n') // Header field terminator
		{
			if (!index || (buffer.data[index - 1] != '\r'))
			{
				status = BadRequest;
				break;
			}
			++index;

			stream_read_flush(stream, index - start);

			length = index - start - 2;
			if (!length) return 0; // no more header fields; success

			// Parse header field and add it to the headers dictionary
			status = http_parse_header_field(header, buffer.data + start, length);
			if (status) break;
			start = index;
			continue;
		}

		++index;
	}

	// Free all headers and the headers dictionary
	dict_term(header);

	return status;
}

// TODO: const, restrict ?
#if !defined(OS_WINDOWS)
static ssize_t interval_insert(off_t (*intervals)[2], size_t count, off_t low, off_t high)
#else
static ssize_t interval_insert(int64_t (*intervals)[2], size_t count, int64_t low, int64_t high)
#endif
{
	ssize_t index;

	// Find the right position for the interval and insert it there. Merge any overlapping intervals.
	for(index = 0; index < count; ++index)
	{
		if (low <= (intervals[index][1] + 1)) // if this is the right position for the new interval
		{
			ssize_t position = index;

			// Find the intervals to merge.
			while (intervals[index][0] <= (high + 1))
			{
				index += 1;
				if (index == count) break;
			}

			// Set the boundaries of the insert interval after the merge.
			if (intervals[position][0] < low) low = intervals[position][0];
			if (high < intervals[index - 1][1]) high = intervals[index - 1][1];

			// Move the intervals to their new positions after the merge.
			ssize_t resize = 1 + (ssize_t)position - (ssize_t)index;
			if (resize > 0) // move to the right
			{
				// WARNING: this works only for resize == 1
				for(index = count - 1; index >= position; --index)
				{
					intervals[index + resize][0] = intervals[index][0];
					intervals[index + resize][1] = intervals[index][1];
				}
			}
			else if (resize < 0) // move to the left
			{
				for(index = position + 1 - resize; index < count; ++index)
				{
					intervals[index + resize][0] = intervals[index][0];
					intervals[index + resize][1] = intervals[index][1];
				}
			}

			// Insert the new interval.
			intervals[position][0] = low;
			intervals[position][1] = high;

			return resize;
		}
	}

	intervals[count][0] = low;
	intervals[count][1] = high;
	return 1;
}

// WARNING: range must be null terminated
// If no error occures, *intervals will always be positive.
// Ranges are stored in a two-dimensional array. Each row is a pair representing a closed interval with the absolute lower and upper position of the range.
#if !defined(OS_WINDOWS)
int http_parse_range(const char *range, off_t content_length, off_t (**ranges)[2], size_t *restrict intervals)
#else
int http_parse_range(const char *range, int64_t content_length, int64_t (**ranges)[2], size_t *restrict intervals)
#endif
{
	// Check whether range unit is supported.
	struct string start = string("bytes=");
	if (memcmp(range, start.data, start.length))
	{
		*ranges = 0;
		return 0;
	}

	// TODO: read the RFC carefully. maybe I should not return BadRequest in some cases

	size_t index;
	size_t count;

	// Count ranges.
	count = 1;
	for(index = start.length; range[index]; ++index)
		if (range[index] == ',')
			count += 1;

	// Allocate memory to strore the ranges.
#if !defined(OS_WINDOWS)
	*ranges = malloc(sizeof(off_t) * 2 * count);
#else
	*ranges = malloc(sizeof(int64_t) * 2 * count);
#endif
	if (!*ranges) return InternalServerError;

	// Parse the ranges and store them.

	index = start.length;
	count = 0;

	size_t from, to;
	char *end;
	do
	{
		// Parse a range specifier.
		if (range[index] == '-')
		{
			// The range specifies last bytes.

			if (!isdigit(range[index + 1])) goto error;

			errno = 0;
			to = strtol(range + index + 1, &end, 10);
			if (!to && (errno == EINVAL)) goto error;
			index = end - range;

			from = (content_length > to) ? (content_length - to) : 0;
			to = content_length - 1;
		}
		else if (isdigit(range[index]))
		{
			// The range specifies first bytes.

			errno = 0;
			from = strtol(range + index, &end, 10);
			if (!from && (errno == EINVAL)) goto error;
			index = end - range;

			if (range[index] != '-') goto error;
			if (range[++index])
			{
				if (!isdigit(range[index])) goto error;

				errno = 0;
				to = strtol(range + index, &end, 10);
				if (!to && (errno == EINVAL)) goto error;
				index = end - range;
			}
			else to = content_length - 1;
		}
		else if (isspace(range[index])) continue;
		else goto error;

		// Store the parsed range specifier if it is satisfiable.
		if ((from <= to) && (from < content_length))
		{
			// Store the range specifiers as a list of sorted closed non-overlapping intervals.
			if (to >= content_length) to = content_length - 1;
			count += interval_insert(*ranges, count, from, to);
		}

		// Look for a separator or terminator.
		while (isspace(range[index])) index += 1;
		if (!range[index]) break; // end of range header
		else if (range[index] != ',') goto error;

	} while (++index);

	if (!count)
	{
		free(*ranges);
		*ranges = 0;
		return RequestedRangeNotSatisfiable;
	}

	// TODO: realloc here to save memory?

	*intervals = count;
	return 0;

error:
	free(*ranges);
	*ranges = 0;
	return BadRequest; // TODO return RequestedRangeNotSatisfiable;
}

static bool option_key_char(char c)
{
	if (iscntrl(c)) return false;
	switch (c)
	{
	case ' ':
	case '(':
	case ')':
	case '<':
	case '>':
	case '@':
	case ',':
	case ';':
	case ':':
	case '\\':
	case '"':
	case '/':
	case '[':
	case ']':
	case '?':
	case '{':
	case '}':
		return false;
	default:
		return true;
	}
}

int http_parse_options(struct dict *restrict options, const struct string *string)
{
	size_t index = 0;
	size_t name_index, value_index, value_length;
	bool last = false;
	struct string name, *value;

	while (1)
	{
		// Get key
		name_index = index;
		if (string->data[index] == '=') return BadRequest;
		do if (!option_key_char(string->data[index])) return BadRequest;
		while (string->data[index++] != '=');

		name = string(string->data + name_index, index - 1 - name_index);

		// Get value
		value_index = index;
		while (1)
		{
			switch (string->data[index])
			{
			case 0:
				last = true;
			case ';':
				goto next;
			case ' ':
			case '"':
			case ',':
			case '\\':
				return BadRequest;
			default:
				if (iscntrl(string->data[index])) return BadRequest;
			}
			++index;
		}
next:

		// Allocate memory for NUL terminated value and copy the url-decoded value there
		value_length = index - value_index;
		value = malloc(sizeof(struct string) + sizeof(char) * (value_length + 1)); // TODO: this can be optimized to allocate less memory
		if (!value) return InternalServerError;
		value->length = value_length;
		value->data = (char *)(value + 1);
		if (value_length)
		{
			value->length = url_decode(string->data + value_index, value->data, value_length);
			if (!value->length) return BadRequest;
		}
		value->data[value->length] = 0;

		// Add the option to the dictionary
		if (dict_add(options, &name, value)) return InternalServerError;

		if (last) break;

		// There should be a space character before the next option
		if (string->data[++index] != ' ') return BadRequest;
		++index;
	}

	return 0;
}

// TODO: this function is just a temporary solution. I should read the RFC and do this properly
// WARNING: string must be NUL-terminated
// TODO: handle errors properly
int http_parse_content_disposition(struct dict *restrict options, const struct string *string)
{
	size_t index = 0;
	size_t name_index, value_index, value_length;
	struct string name, *value;

	// Skip the main part. Get only arguments
	while ((index < string->length) && (string->data[index] != ';'))
		if (++index == 64) return UnsupportedMediaType;
	if ((index + 2) >= string->length) return BadRequest;
	if (string->data[++index] != ' ') return BadRequest;
	++index;

	while (1)
	{
		// Get key
		name_index = index;
		if (string->data[index] == '=') return BadRequest;
		do if (!option_key_char(string->data[index])) return BadRequest;
		while (string->data[index++] != '=');

		name = string(string->data + name_index, index - 1 - name_index);

		// Get value
		// TODO: check for the right format here
		if (string->data[index] != '"') return BadRequest;
		value_index = ++index;
		while (string->data[index] != '"')
		{
			if (iscntrl(string->data[index])) return BadRequest;
			++index;
		}

		// Allocate memory for NUL terminated value and copy the url-decoded value there
		value_length = index - value_index;
		value = malloc(sizeof(struct string) + sizeof(char) * (value_length + 1));
		if (!value) return InternalServerError;
		value->data = (char *)(value + 1);
		if (value_length)
		{
			value->length = url_decode(string->data + value_index, value->data, value_length);
			if (!value->length) return BadRequest;
		}
		else value->length = value_length;
		value->data[value->length] = 0;

		// Add the option to the dictionary
		if (dict_add(options, &name, value)) return InternalServerError;

		++index;
		if (!string->data[index]) break;
		else if (string->data[index] != ';') return BadRequest;

		// There should be a space character before the next option
		if (string->data[++index] != ' ') return BadRequest;
		++index;
	}

	return 0;
}

int http_parse_version(short version[restrict 2], struct stream *restrict stream)
{
	struct string buffer;
	size_t start, delimiter = 0;

	// Read at least as much characters as the shortest valid version string
	if (stream_read(stream, &buffer, sizeof("HTTP/?.? ") - 1)) return -1;

	start = sizeof("HTTP/") - 1;
	if (memcmp(buffer.data, "HTTP/", start)) return BadRequest;

	// Find the end of the version string
	size_t index = start;
	while (true)
	{
		if (buffer.data[index] == '.')
		{
			if (delimiter) return BadRequest;
			else delimiter = index;
		}
		else if (isspace(buffer.data[index]))
		{
			// Version always consists of major and minor numbers.
			// Both of them should be at least one digit long
			if (!delimiter || !(delimiter - start) || !(index - delimiter - 1)) return BadRequest;

			// Previous checks assure that these operations are always successful
			version[0] = (short)strtol(buffer.data + start, 0, 10);
			version[1] = (short)strtol(buffer.data + delimiter + 1, 0, 10);

			// Success
			stream_read_flush(stream, index);
			break;
		}
		else if (!isdigit(buffer.data[index])) return BadRequest;

		if (index >= sizeof("HTTP/??.?? ") - 1) return BadRequest; // TODO: check this status code

		if (++index < buffer.length) continue;

		if (stream_read(stream, &buffer, buffer.length + 1)) return -1;
	}

	return 0;
}

// WARNING: Comments are not supported.
// WARNING: Only single digit version numbers are supported.

// character classes
enum
{
	C_I,	// invalid (any non-ascii or control character)
	C_LWS,	// ' ' or \t
	C_CR,	// \r
	C_LF,	// \n
	C_D,	// 0-9
	C_H,	// H
	C_T,	// T
	C_P,	// P
	C_L,	// letter different from H, T and P
	C_S,	// /
	C_QQ,	// "
	C_BS,	// backslash
	C_DOT,	// .
	C_COL,	// :
	C_PUN,	// punctuation ! # $ % & ' * + - ^ _ ` | ~
	C_SEP,	// separator ( ) [ ] { } < > , ; ? = @
};
#define CLASSES_COUNT 16

static unsigned char class[256] = {
	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_LWS,	C_LF,	C_I,	C_I,	C_CR,	C_I,	C_I,
	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,	C_I,

//	 		!		"		#		$		%		&		'		(		)		*		+		,		-		.		/
	C_LWS,	C_PUN,	C_QQ,	C_PUN,	C_PUN,	C_PUN,	C_PUN,	C_PUN,	C_SEP,	C_SEP,	C_PUN,	C_PUN,	C_SEP,	C_PUN,	C_DOT,	C_S,

//	0		1		2		3		4		5		6		7		8		9		:		;		<		=		>		?
	C_D,	C_D,	C_D,	C_D,	C_D,	C_D,	C_D,	C_D,	C_D,	C_D,	C_COL,	C_SEP,	C_SEP,	C_SEP,	C_SEP,	C_SEP,

//	@		A		B		C		D		E		F		G		H		I		J		K		L		M		N		O
	C_SEP,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_H,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,

//	P		Q		R		S		T		U		V		W		X		Y		Z		[		\		]		^		_
	C_P,	C_L,	C_L,	C_L,	C_T,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_SEP,	C_BS,	C_SEP,	C_PUN,	C_PUN,

//	`		a		b		c		d		e		f		g		h		i		j		k		l		m		n		o
	C_PUN,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,

//	p		q		r		s		t		u		v		w		x		y		z		{		|		}		~
	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_L,	C_SEP,	C_PUN,	C_SEP,	C_PUN,

// 127 - 255 are auto-initialized to 0
};

// states (determines what character is expected next)
enum
{
	S_MF,	// first method character or \r
	S_ML,	// \n before first method character
	S_M,	// method character
	S_UF,	// first URI character
	S_U,	// URI character
	S_H,
	S_HT,
	S_HTT,
	S_HTTP,
	S_VS,	// version /
	S_VJ,	// major version
	S_VP,	// version .
	S_VI,	// minor version
	S_FC,	// \r before first header
	S_FL,	// \n before first header
	S_N,	// header name
	S_NC,	// header name or :
	S_NE,	// header name or whitespace or \r
	S_V,	// header value or \r or "
	S_VQ,	// header value or \\ or closing "
	S_VE,	// escaped header value
	S_VL,	// \n before header
	S_E,	// end of headers \n
};
#define START 0
#define FIN	24
#define STATES_COUNT 23

// TODO replace -1 with the appropriate error code

static const char state[STATES_COUNT][CLASSES_COUNT] = {
//	invalid	sp/tab	\r		\n		digit	H		T		P		letter	/		"		\		.		:		punc	sep
	-1,		-1,		S_ML,	-1,		-1,		S_M,	S_M,	S_M,	S_M,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_MF	first method character or \r
	-1,		-1,		-1,		S_MF,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_ML	\n before first method character
	-1,		S_UF,	-1,		-1,		-1,		S_M,	S_M,	S_M,	S_M,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_M	method character
	-1,		-1,		-1,		-1,		S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	//S_UF	first URI character
	-1,		S_H,	-1,		-1,		S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	S_U,	//S_U	URI character
	-1,		-1,		-1,		-1,		-1,		S_HT,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_H
	-1,		-1,		-1,		-1,		-1,		-1,		S_HTT,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_HT
	-1,		-1,		-1,		-1,		-1,		-1,		S_HTTP,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_HTT
	-1,		-1,		-1,		-1,		-1,		-1,		-1,		S_VS,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_HTTP
	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		S_VJ,	-1,		-1,		-1,		-1,		-1,		-1,		//S_VS	version /
	-1,		-1,		-1,		-1,		S_VP,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_VJ	major version
	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		S_VI,	-1,		-1,		-1,		//S_VP	version .
	-1,		-1,		-1,		-1,		S_FC,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_VI	minor version
	-1,		-1,		S_FL,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_FC	\r before first header
	-1,		-1,		-1,		S_N,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_FL	\n before first header
	-1,		-1,		-1,		-1,		S_NC,	S_NC,	S_NC,	S_NC,	S_NC,	-1,		-1,		-1,		S_NC,	-1,		S_NC,	-1,		//S_N	header name
	-1,		-1,		-1,		-1,		S_NC,	S_NC,	S_NC,	S_NC,	S_NC,	-1,		-1,		-1,		S_NC,	S_V,	S_NC,	-1,		//S_NC	header name or :
	-1,		S_V,	S_E,	-1,		S_NC,	S_NC,	S_NC,	S_NC,	S_NC,	-1,		-1,		-1,		S_NC,	-1,		S_NC,	-1,		//S_NE	header name or whitespace or \r
	-1,		S_V,	S_VL,	-1,		S_V,	S_V,	S_V,	S_V,	S_V,	S_V,	S_VQ,	S_V,	S_V,	S_V,	S_V,	S_V,	//S_V	header value or \r or "
	-1,		S_VQ,	-1,		-1,		S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_V,	S_VE,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	//S_VQ	header value or \\ or closing "
	-1,		S_VQ,	-1,		-1,		S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	S_VQ,	//S_VE	escaped header value
	-1,		-1,		-1,		S_NE,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_VL	\n before header
	-1,		-1,		-1,		FIN,	-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		-1,		//S_E	end of headers \n
};

bool http_parse_init(struct http_context *restrict context)
{
	memset(&context->request, 0, sizeof(context->request));

	if (!dict_init(&context->request.headers, DICT_SIZE_BASE)) return false; // InternalServerError

	context->index = 0;
	context->state = START;

	return true;
}

void http_parse_term(struct http_context *restrict context)
{
	free(context->request.URI.data);
	context->request.URI.data = 0; // TODO is this necessary?
	dict_term(&context->request.headers); // TODO is this enough?
}

static int header_add(struct dict *restrict headers, char *restrict buffer, size_t length, size_t separator)
{
	struct string key = string(buffer, separator);
	buffer += separator + 1;
	length -= separator + 1;
	struct string *value = malloc(sizeof(struct string) + length + 1);
	if (!value) return ERROR_MEMORY;
	value->data = (char *)(value + 1);

	// Convert header name to lower case.
	separator -= 1;
	do key.data[separator] = tolower(key.data[separator]);
	while (separator--);

	// Trim whitespace characters and replace any sequence of whitespace characters with a single space.
	bool space = false;
	size_t src;
	size_t index;
	index = 0;
	for(src = 0; src < length; ++src)
	{
		if (isspace(buffer[src]))
		{
			if (index) space = true;
		}
		else if (buffer[src] == '\\')
		{
			if (++src == length) return -1; // TODO
			switch (buffer[src]) // TODO support escape sequences
			{
			default:
				value->data[index++] = buffer[src];
			}
		}
		else
		{
			if (space)
			{
				space = false;
				value->data[index++] = ' ';
			}
			value->data[index++] = buffer[src];
		}
	}

	value->data[index] = 0;
	value->length = index;
	int status = dict_add(headers, &key, value);
	if (status) free(value);
	return status;
}

int http_parse(struct http_context *restrict context, struct stream *restrict stream)
{
	int status;

	struct string buffer;
	size_t cached = stream_cached(stream);
	if (cached >= URI_LENGTH_MAX) return RequestURITooLong; // TODO: change this
	if (status = stream_read(stream, &buffer, cached + 1)) return status;

	unsigned char byte;
	char state_new;
	struct string token;
	for(; context->index < buffer.length; context->index += 1)
	{
		byte = class[(unsigned char)buffer.data[context->index]];
		state_new = state[(unsigned char)context->state][(unsigned)byte];

		if (state_new == context->state) continue; // noting to do if state hasn't changed

		switch (state_new)
		{
		case S_M: // first character of method
			context->start = context->index;
			break;

		case S_UF: // space after method
			token = string(buffer.data + context->start, context->index - context->start);

			#define METHOD_IS(m) ((token.length == (sizeof(m) - 1)) && !memcmp(token.data, (m), sizeof(m) - 1))
			if METHOD_IS("HEAD") context->request.method = METHOD_HEAD;
			else if METHOD_IS("GET") context->request.method = METHOD_GET;
			else if METHOD_IS("POST") context->request.method = METHOD_POST;
			else if METHOD_IS("OPTIONS") context->request.method = METHOD_OPTIONS;
			else if METHOD_IS("PUT") context->request.method = METHOD_PUT;
			else if METHOD_IS("DELETE") context->request.method = METHOD_DELETE;
			else if METHOD_IS("SUBSCRIBE") context->request.method = METHOD_SUBSCRIBE;
			else if METHOD_IS("NOTIFY") context->request.method = METHOD_NOTIFY;
			else return NotImplemented; // TODO
			#undef METHOD_IS

			context->start = context->index + 1;
			break;

		case S_H: // space after URI
			token = string(buffer.data + context->start, context->index - context->start);

			context->request.URI.data = malloc(token.length + 1);
			if (!context->request.URI.data) return InternalServerError;
			*format_bytes(context->request.URI.data, token.data, token.length) = 0;
			context->request.URI.length = token.length;

			stream_read_flush(stream, context->index);
			stream_read(stream, &buffer, buffer.length - context->index); // read data is already buffered
			context->index = 0;

			break;

		case S_VP: // major version
			errno = 0;
			context->request.version[0] = strtol(buffer.data + context->index, 0, 10);
			if (errno) return BadRequest; // TODO
			break;

		case S_FC: // minor version
			errno = 0;
			context->request.version[1] = strtol(buffer.data + context->index, 0, 10);
			if (errno) return BadRequest; // TODO
			break;

		case S_E: // end of header
			// assert(context->state == S_NE);
			goto add;
		case S_NC: // first character of header name
			if (context->state == S_NE) // there was a header before this one
			{
		add:
				// TODO check return value
				header_add(&context->request.headers, buffer.data + context->start, context->index - context->start, context->separator - context->start);

				stream_read_flush(stream, context->index);
				stream_read(stream, &buffer, buffer.length - context->index); // read data is already buffered
				context->index = 0;
			}

			context->start = context->index;
			break;

		case S_V: // header value
			if (context->state == S_NC) context->separator = context->index; // first character of header value
			break;

		case FIN:
			stream_read_flush(stream, context->index + 1);
			return 0; // success

		case -1:
			return ERROR_INPUT;
		}

		context->state = state_new;
	}

	return ERROR_AGAIN; // header not read
}

static int http_parse_quality(const char *data, size_t length)
{
	// /q=(0(\.\d{,3})?|1(\.0{,3})?)/

	if ((length < 3) || (7 < length) || (data[0] != 'q') || (data[1] != '=')) return -1;

	int result;

	// Parse first digit
	result = (data[0] - '0') * 1000;
	if ((result < 0) || (1000 < result)) return -1;

	if (length == 3) return result;
	if (data[3] != '.') return -1;

	if (length > 4)
	{
		if (!isdigit(data[4])) return -1;
		result += (data[4] - '0') * 100;
		if (length > 5)
		{
			if (!isdigit(data[5])) return -1;
			result += (data[5] - '0') * 10;
			if (length > 6)
			{
				if (!isdigit(data[6])) return -1;
				result += (data[6] - '0');
			}
		}
	}

	return result;
}

static int http_parse_accept_add(const char *buffer, size_t item_length, size_t quality_length, struct string *restrict list, unsigned *priorities, size_t *restrict allow, size_t *restrict deny)
{
	int priority;
	struct string item;
	size_t last;

	// Generate access item.
	item.length = item_length;
	item.data = malloc(item.length + 1);
	if (!item.data) return InternalServerError; // memory error
	memcpy(item.data, buffer, item.length);
	item.data[item.length] = 0;

	if (quality_length > 0) // quality is specified
	{
		priority = http_parse_quality(buffer + item_length, quality_length);
		if (priority < 0) return BadRequest;
		else if (!priority)
		{
			// The specified item is not allowed. Add it to the deny list.
			list[*allow + (*deny)++] = item;
			return 0;
		}
	}
	else priority = 1;

	// Find the right position for the item and put it there.
	for(last = *allow; (last && (priorities[last - 1] < priority)); --last)
	{
		list[last] = list[last - 1];
		priorities[last] = priorities[last - 1];
	}
	list[last] = item;
	priorities[last] = priority;
	*allow += 1;

	return 0;
}

// TODO: some strings may not be parsed correctly
// WARNING: header must be NUL terminated
// Initializes array with accepted and not accepted values.
int http_parse_accept(const struct string *header, struct string **list, size_t *restrict allow, size_t *restrict deny)
{
	size_t index;

	size_t count = 0;

	// Determine accept values count.
	bool data = false;
	for(index = 0; index < header->length; ++index)
	{
		if (header->data[index] == ',')
		{
			count += data;
			data = false;
		}
		else if (!isspace(header->data[index])) data = true;
	}
	count += data;

	// Initialize temporary priority array so that results can be sorted by priority.
	// Initialize data structures.
	unsigned *priorities = malloc(sizeof(unsigned) * count);
	if (!priorities) return InternalServerError;
	*list = malloc(sizeof(struct string) * count);
	if (!*list)
	{
		free(priorities);
		return InternalServerError;
	}
	*allow = 0;
	*deny = 0;

	ssize_t start, colon;
	bool ready = true;

	int status;

	start = -1;
	colon = -1;

	for(index = 0; index < header->length; ++index)
	{
		if (header->data[index] == ',') // separator
		{
			if (start >= 0)
			{
				if (colon < 0) colon = index;
				status = http_parse_accept_add(header->data + start, colon - start, index - colon, *list, priorities, allow, deny);
				if (status) goto error;
				start = -1;
				colon = -1;
			}

			ready = true;
		}
		else if (isspace(header->data[index]))
		{
			if (start >= 0)
			{
				if (colon < 0) colon = index;
				status = http_parse_accept_add(header->data + start, colon - start, index - colon, *list, priorities, allow, deny);
				if (status) goto error;
				start = -1;
				colon = -1;

				ready = false;
			}
		}
		else
		{
			if (start >= 0)
			{
				if (header->data[index] == ';') colon = index;
			}
			else if (ready) start = index;
			else
			{
				status = BadRequest;
				goto error;
			}
		}
	}
	if (start >= 0)
	{
		if (colon < 0) colon = index;
		status = http_parse_accept_add(header->data + start, colon - start, index - colon, *list, priorities, allow, deny);
		if (status) goto error;
	}

	free(priorities);
	count = *allow + *deny;
	*list = realloc(*list, sizeof(struct string) * count);

	return 0;

error:
	free(priorities);
	count = *allow + *deny;
	while (count--) free((*list)[count].data);
	free(*list);

	return status;
}
