#define METHOD_HEAD 1
#define METHOD_GET 2
#define METHOD_POST 3
#define METHOD_OPTIONS 4
#define METHOD_PUT 5
#define METHOD_DELETE 6
#define METHOD_SUBSCRIBE 7
#define METHOD_NOTIFY 8

#define PROTOCOL_HTTP 1
#define PROTOCOL_HTTPS 2

struct http_request
{
	// Fields common for all methods
	unsigned method;
	struct string URI;
	short version[2];
	struct dict headers;
	const struct string *hostname;

	// Fields specific to some methods
	unsigned protocol, port; // 0 == default
	struct string path;
	union json *query;
};

struct http_context
{
	struct http_request request;
	size_t index;
	size_t start, separator;
	char state;

	int control; // pipe file descriptor for control messages
};

// Each http_parse* function returns:
//  0	Success
// -1	Connection error
// >0	HTTP error

int http_parse_uri(struct http_request *restrict request);

// WARNING: string must be NUL-terminated
#if !defined(OS_WINDOWS)
int http_parse_range(const char *range, off_t content_length, off_t (**ranges)[2], size_t *restrict intervals);
#else
int http_parse_range(const char *range, int64_t content_length, int64_t (**ranges)[2], size_t *restrict intervals);
#endif

int http_parse_accept(const struct string *header, struct string **list, size_t *restrict allow, size_t *restrict deny);

int http_parse_content_disposition(struct dict *restrict options, const struct string *string);

// WARNING: string must be NUL-terminated
int http_parse_options(struct dict *restrict options, const struct string *string);

int http_parse_header(struct dict *restrict header, struct stream *restrict stream);

int http_parse_version(short version[restrict 2], struct stream *restrict stream);

int http_parse(struct http_context *restrict context, struct stream *restrict stream);

#if defined(OS_WINDOWS)
int http_parse_windows(struct http_request *restrict request, struct stream *restrict stream);
#endif

bool http_parse_init(struct http_context *restrict context);
void http_parse_term(struct http_context *restrict context);
