struct resources; // TODO: remove this

#define HEADERS_LENGTH_MAX 1024

struct http_response
{
	char headers[HEADERS_LENGTH_MAX];
	char *headers_end;
	unsigned code;

	int content_encoding;
	
#if !defined(OS_WINDOWS)
	off_t (*ranges)[2];
	size_t intervals;
	off_t index, length; // used for range requests
#else
	int64_t (*ranges)[2];
	size_t intervals;
	__int64 index, length;
#endif
};

int http_errno_status(int error);

//bool header_add(struct vector *restrict headers, const struct string *key, const struct string *value);
bool response_header_add(struct http_response *restrict response, const struct string *key, const struct string *value);

int handler_static(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources);
int handler_dynamic(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources);

int response_cache(const char *restrict key, const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources);

off_t content_length(const struct dict *restrict headers);

bool response_headers_send(struct stream *restrict stream, const struct http_request *request, struct http_response *restrict response, off_t length);
int response_entity_send(struct stream *restrict stream, struct http_response *restrict response, const char *restrict data, off_t length);

// WARNING: deprecated; use response_entity_send() instead
#define response_content_send(stream, response, data, length) (!response_entity_send((stream), (response), (data), (length)))

#define response_chunk_last(stream, response) response_content_send((stream), (response), "", 0)

#define RESPONSE_CHUNKED -1
