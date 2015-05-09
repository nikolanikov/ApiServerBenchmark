#define TIMEOUT 10000 /* 10s */

#define BUFFER_SIZE_MIN 1024	/* 1 KiB */
#define BUFFER_SIZE_MAX 65536	/* 64 KiB */

struct stream
{
	char *_input;
	size_t _input_size, _input_index, _input_length;

	char *_output;
	size_t _output_size, _output_index, _output_length;

	int fd;
#if defined(TLS)
	void *_tls;
	size_t _tls_retry; // amount of data that could not be written without blocking on the last request
#endif
};

#if defined(TLS)
int tls_init(void);
void tls_term(void);

int stream_init_tls_connect(struct stream *restrict stream, int fd, const char *restrict domain);
int stream_init_tls_accept(struct stream *restrict stream, int fd);
#endif

int stream_init(struct stream *restrict stream, int fd);
int stream_term(struct stream *restrict stream);

size_t stream_cached(const struct stream *stream);

int stream_read(struct stream *restrict stream, struct string *restrict buffer, size_t length);
void stream_read_flush(struct stream *restrict stream, size_t length);

int stream_write(struct stream *restrict stream, const struct string *buffer);
int stream_write_flush(struct stream *restrict stream);
