#define OK 200
#define NoContent 204
#define PartialContent 206
#define MovedPermanently 301
#define NotModified 304
#define BadRequest 400
#define Forbidden 403
#define NotFound 404
#define MethodNotAllowed 405
// #define NotAcceptable 406
#define RequestTimeout 408
// #define Conflict 409
// #define Gone 410
#define LengthRequired 411
// #define PreconditionFailed 412
#define RequestEntityTooLarge 413
#define RequestURITooLong 414
#define UnsupportedMediaType 415
#define RequestedRangeNotSatisfiable 416
// #define ExpectationFailed 417
#define InternalServerError 500
#define NotImplemented 501
#define BadGateway 502
#define ServiceUnavailable 503

#define HTTP_DATE_LENGTH 29

size_t url_decode(const char *src, char *restrict dest, size_t length);
struct string *restrict uri_encode(const char *restrict source, size_t size);

void http_date(char buffer[HTTP_DATE_LENGTH + 1], time_t timestamp);

void http_open(int sock);
void http_close(int sock);

/*
#if !defined(OS_WINDOWS)
off_t http_chunked(struct stream *restrict input, int output, const struct string *restrict key);
#else
int64_t http_chunked(struct stream *restrict input, int output, const struct string *restrict key);
#endif
*/
