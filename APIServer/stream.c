#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

# include <poll.h>

#include "base.h"
#include "stream.h"

#define terminated(stream) (!(stream)->_input)

// TODO: read about TLS packet size (negotiated maximum record size)
#define TLS_RECORD		1024

#define WRITE_MAX 8192 /* rename this */

// When a flush operation is performed, if the corresponding buffer (input or output) is empty, its size is shrinked to the minimum allowed size.

// Priority strings:
// http://gnutls.org/manual/html_node/Priority-Strings.html
// http://gnutls.org/manual/html_node/Supported-ciphersuites.html#ciphersuites
// http://unhandledexpression.com/2013/01/25/5-easy-tips-to-accelerate-ssl/

// TODO consider using readv and circular read buffer http://stackoverflow.com/questions/3575424/buffering-data-from-sockets

// TODO: don't allow operations on a terminated stream

// http://docs.fedoraproject.org/en-US/Fedora_Security_Team//html/Defensive_Coding/sect-Defensive_Coding-TLS-Client-GNUTLS.html

// GNU TLS pitfalls
//  http://lists.gnu.org/archive/html/help-gnutls/2009-12/msg00011.html
//  http://lists.gnutls.org/pipermail/gnutls-help/2002-August.txt
//  http://lists.gnutls.org/pipermail/gnutls-help/2009-December/001901.html
//  http://docs.fedoraproject.org/en-US/Fedora_Security_Team//html/Defensive_Coding/chap-Defensive_Coding-TLS.html#sect-Defensive_Coding-TLS-Pitfalls-GNUTLS
//  http://docs.fedoraproject.org/en-US/Fedora_Security_Team//html/Defensive_Coding/chap-Defensive_Coding-TLS.html#ex-Defensive_Coding-TLS-Nagle

// TODO move this somewhere else
// Supported functions:
// open(), flock()
// stat(), fstat(), lstat()
// read(), readv(), write()
// opendir()
// readdir_r()
// mmap()
// mkdir()
// socket(), poll(), connect()
int errno_error(int code)
{
	switch (code)
	{
	case ENOMEM:
	case EMFILE:
	case ENFILE:
	case EDQUOT:
	case ENOBUFS:
	case EMLINK:
	case EISCONN: // TODO is this right (doesn't look right for connect())
	case EADDRNOTAVAIL:
	case ENOLCK:
		return ERROR_MEMORY;

	case EACCES:
	case EPERM:
		return ERROR_ACCESS;

	case EEXIST:
	case EADDRINUSE:
		return ERROR_EXIST;

	case ELOOP:
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
	case ENXIO: // TODO this doesn't seem right for write()
		return ERROR_MISSING;

	case EFAULT:
	case EINVAL:
	case EBADF:
	case ENOTSOCK:
	case EALREADY:
	case ENOTSUP:
# if (EOPNOTSUPP != ENOTSUP)
	case EOPNOTSUPP:
# endif
		return ERROR_INPUT;

	case ETXTBSY:
	case ETIMEDOUT:
	case EINTR: // TODO is this right?
	case EAGAIN:
# if (EWOULDBLOCK != EAGAIN)
	case EWOULDBLOCK:
# endif
		return ERROR_AGAIN;

	case EIO:
	case ENOSPC: // TODO this does not seem right for rename()
	case EBUSY:
	case ENOTEMPTY:
		return ERROR_EVFS;

	case EPIPE:
		return ERROR_WRITE;

	case EAFNOSUPPORT:
	case EPROTONOSUPPORT:
	case EPROTOTYPE:
	case EXDEV:
		return ERROR_UNSUPPORTED;

	case EHOSTUNREACH:
	case ENETDOWN:
	case ENETUNREACH:
	case ECONNREFUSED:
	case ECONNRESET:
		return ERROR_NETWORK;

	case EINPROGRESS:
		return ERROR_PROGRESS;

	default:
		// TODO ENOTDIR
		// TODO EROFS
		// TODO EISDIR
		// TODO ENOTCONN
		// TODO EFBIG EDQUOT ENOSPC EDESTADDRREQ 
		// TODO rmdir() can return EBUSY ENOTEMPTY; consider returning a different error for them
		// TODO rmdir() and rename() may return EEXIST to mean that the directory is not empty on some POSIX systems
		return ERROR;
	}
}

#if defined(TLS)
// TLS implementation based on X.509

# include "gnutls/gnutls.h"					// libgnutls

// certificate authorities
#if defined(OS_MAC)
# define TLS_CA "/Applications/Filement.app/Contents/Resources/ca.crt"
#elif !defined(OS_WINDOWS)
# define TLS_CA "/usr/local/share/filement/ca.crt"
#else
extern char *tls_location;
# define TLS_CA tls_location
#endif

// certificate revocation lists
//# define CRLFILE "/etc/filement/crl.pem"

// certificate
# if !TEST
#  define TLS_CERT_FILE "/etc/filement/test.p12"
#  define TLS_CERT_PASSWORD "webc0nnect"
# else
#  define TLS_CERT_FILE "/etc/filement/filement.crt"
#  define TLS_KEY_FILE "/etc/filement/filement.key"
# endif

// To generate certificate do this:
// master.webconnect.bg
// certtool --load-certificate /etc/lighttpd/filement.pem --load-privkey /etc/lighttpd/filement.pem --load-ca-certificate /etc/lighttpd/sf_bundle.crt --to-p12 --outfile "test.p12"

static gnutls_certificate_credentials_t x509;
static gnutls_dh_params_t dh_params;

//printf("%s\n", (char *)gnutls_strerror(status));

int tls_init(void)
{
	if (gnutls_global_init() != GNUTLS_E_SUCCESS) return -1;

	if (gnutls_certificate_allocate_credentials(&x509) != GNUTLS_E_SUCCESS)
	{
		gnutls_global_deinit();
		return -1;
	}

	if (gnutls_certificate_set_x509_trust_file(x509, TLS_CA, GNUTLS_X509_FMT_PEM) < 0)
	{
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}
	//gnutls_certificate_set_x509_crl_file(x509, CRLFILE, GNUTLS_X509_FMT_PEM); // TODO: certificate revokation list

# if !defined(DEVICE) /* devices don't have certificates */
#  if !TEST
	if (gnutls_certificate_set_x509_simple_pkcs12_file(x509, TLS_CERT_FILE, GNUTLS_X509_FMT_PEM, TLS_CERT_PASSWORD) != GNUTLS_E_SUCCESS)
#  else
	if (gnutls_certificate_set_x509_key_file(x509, TLS_CERT_FILE, TLS_KEY_FILE, GNUTLS_X509_FMT_PEM) != GNUTLS_E_SUCCESS)
#  endif
	{
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}

	// Generate and set prime numbers for key exchange.
	// TODO These should be discarded and regenerated once a day, once a week or once a month, depending on the security requirements.
	if (gnutls_dh_params_init(&dh_params) != GNUTLS_E_SUCCESS)
	{
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}
	if (gnutls_dh_params_generate2(dh_params, gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, GNUTLS_SEC_PARAM_LOW)) != GNUTLS_E_SUCCESS) goto error; // TODO: change GNUTLS_SEC_PARAM
	gnutls_certificate_set_dh_params(x509, dh_params);
# endif

	gnutls_global_set_log_level(1); // TODO change this

	return 0;

error:
	tls_term();
	return -1;
}

void tls_term(void)
{
# if !defined(DEVICE) /* devices don't have this */
	gnutls_dh_params_deinit(dh_params);
# endif
	gnutls_certificate_free_credentials(x509);
	gnutls_global_deinit();
}

static inline int stream_init_tls(struct stream *restrict stream, int fd, void *tls)
{
	// TODO: use gnutls_record_get_max_size()

	stream->_input = malloc(BUFFER_SIZE_MIN);
	if (!stream->_input) return ERROR_MEMORY;
	stream->_input_size = BUFFER_SIZE_MIN;
	stream->_input_index = 0;
	stream->_input_length = 0;

	stream->_output = malloc(BUFFER_SIZE_MIN);
	if (!stream->_output)
	{
		free(stream->_input);
		stream->_input = 0;
		return ERROR_MEMORY;
	}
	stream->_output_size = BUFFER_SIZE_MIN;
	stream->_output_index = 0;
	stream->_output_length = 0;

# if !defined(OS_WINDOWS)
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
# endif
	stream->fd = fd;

	stream->_tls = tls;
	stream->_tls_retry = 0;

	return 0;
}
#include <stdio.h>
int stream_init_tls_connect(struct stream *restrict stream, int fd, const char *restrict domain)
{
	int status;

	gnutls_session_t session;
	if (gnutls_init(&session, GNUTLS_NONBLOCK | GNUTLS_CLIENT) != GNUTLS_E_SUCCESS) return -1;

	if (gnutls_priority_set_direct(session, "NORMAL", 0) != GNUTLS_E_SUCCESS) goto error;
	if (gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509) != GNUTLS_E_SUCCESS) goto error;

	// Perform TLS handshake on the socket.
	gnutls_transport_set_ptr(session, (gnutls_transport_ptr_t)(ptrdiff_t)fd);
	while ((status = gnutls_handshake(session)) < 0)
		if (gnutls_error_is_fatal(status))
			goto error;

	// TODO: stream_term on the errors below?
	// Validate server certificate.
	unsigned errors = UINT_MAX; // set all bits
#if !TEST
	if (gnutls_certificate_verify_peers3(session, domain, &errors) != GNUTLS_E_SUCCESS) goto error;
#else
	if (gnutls_certificate_verify_peers3(session, 0, &errors) != GNUTLS_E_SUCCESS) goto error;
#endif
	gnutls_datum_t out;
	gnutls_certificate_verification_status_print(errors, gnutls_certificate_type_get(session), &out, 0);
	printf("%s\n", out.data);
	gnutls_free(out.data);
	if (errors) goto error;

	// TODO: check revokation lists

	return stream_init_tls(stream, fd, session);

error:
	gnutls_deinit(session);
	return -1;
}
int stream_init_tls_accept(struct stream *restrict stream, int fd)
{
	int status;

	gnutls_session_t session;
	if (gnutls_init(&session, GNUTLS_NONBLOCK | GNUTLS_SERVER) != GNUTLS_E_SUCCESS) return -1;

	if (gnutls_priority_set_direct(session, "PERFORMANCE:-CIPHER-ALL:+ARCFOUR-128:+AES-128-CBC:+AES-128-GCM", 0) != GNUTLS_E_SUCCESS) goto error;
	//if (gnutls_priority_set_direct(session, "PERFORMANCE:-CIPHER-ALL:+ARCFOUR-128", 0) != GNUTLS_E_SUCCESS) goto error;
	if (gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509) != GNUTLS_E_SUCCESS) goto error;

	// We request no certificate from the client. Otherwise we would need to verify it.
	// gnutls_certificate_server_set_request(session, GNUTLS_CERT_REQUEST);

	// Perform TLS handshake on the socket.
	gnutls_transport_set_ptr(session, (gnutls_transport_ptr_t)(ptrdiff_t)fd);
	while ((status = gnutls_handshake(session)) < 0)
		if (gnutls_error_is_fatal(status))
			goto error;

	//printf("CIPHER: %s\n", gnutls_cipher_get_name(gnutls_cipher_get(session)));

	return stream_init_tls(stream, fd, session);

error:
	gnutls_deinit(session);
	return -1;
}
#endif /* TLS */

int stream_init(struct stream *restrict stream, int fd)
{
	stream->_input = malloc(BUFFER_SIZE_MIN);
	if (!stream->_input) return ERROR_MEMORY;
	stream->_input_size = BUFFER_SIZE_MIN;
	stream->_input_index = 0;
	stream->_input_length = 0;

	stream->_output = malloc(BUFFER_SIZE_MIN);
	if (!stream->_output)
	{
		free(stream->_input);
		stream->_input = 0;
		return ERROR_MEMORY;
	}
	stream->_output_size = BUFFER_SIZE_MIN;
	stream->_output_index = 0;
	stream->_output_length = 0;

#if !defined(OS_WINDOWS)
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
	stream->fd = fd;

#if defined(TLS)
	stream->_tls = 0;
	stream->_tls_retry = 0;
#endif

	return 0;
}

int stream_term(struct stream *restrict stream)
{
	if (terminated(stream)) return true;

	free(stream->_input);
	stream->_input = 0;

	free(stream->_output);
	stream->_output = 0;

#if defined(TLS)
	if (stream->_tls)
	{
		// TODO: check gnutls_bye return status
		int status = gnutls_bye(stream->_tls, GNUTLS_SHUT_RDWR); // TODO: can this modify errno ?
		gnutls_deinit(stream->_tls);
		return (status == GNUTLS_E_SUCCESS);
	}
#endif

	return true;
}

size_t stream_cached(const struct stream *stream)
{
#if defined(TLS)
	return ((stream->_input_length - stream->_input_index) + (stream->_tls ? gnutls_record_check_pending(stream->_tls) : 0));
#else
	return (stream->_input_length - stream->_input_index);
#endif
}

static int timeout(int fd, short event)
{
	struct pollfd wait = {
		.fd = fd,
		.events = event,
		.revents = 0
	};
	int status;

	while (1)
	{
		status = poll(&wait, 1, TIMEOUT);
		if (status > 0)
		{
			if (wait.revents & event) return 0;
			else return ERROR_NETWORK;
		}
		else if ((status < 0) && ((errno == EINTR) || (errno == EAGAIN))) continue;
		else return ERROR_AGAIN;
	}
}

int stream_read(struct stream *restrict stream, struct string *restrict buffer, size_t length)
{
	size_t available = stream->_input_length - stream->_input_index;

	// If input buffer is not big enough, resize it. Realign buffer data if necessary
	if (length > stream->_input_size)
	{
		// Round up buffer size to a multiple of 256 to avoid multiple +1B resizing and 1B reading.
		size_t size = (length + 0xff) & ~0xff;

		char *buffer;

		if (length > BUFFER_SIZE_MAX) return ERROR_MEMORY; // TODO: is this okay?

		if (available) // the buffer has data that should be kept after resizing
		{
			buffer = realloc(stream->_input, sizeof(char) * size);
			if (!buffer)
			{
				free(stream->_input);
				stream->_input = 0;
				return ERROR_MEMORY;
			}

			// Move the available data to the beginning of the buffer if one of these holds:
			//  size is not enough to fit the requested data with the current buffer data layout
			//  at least half of the buffer is wasted
			if (((length - stream->_input_index) < length) || (stream->_input_size <= stream->_input_index * 2))
			{
				// Move byte by byte because the source and the destination overlap.
				if (stream->_input_index) memmove(buffer, buffer + stream->_input_index, available);

				stream->_input_index = 0;
				stream->_input_length = available;
			}
		}
		else // The buffer contains no useful data
		{
			// Free the old buffer and allocate a new one
			free(stream->_input);
			buffer = malloc(sizeof(char) * size);
			if (!buffer)
			{
				stream->_input = 0;
				return ERROR_MEMORY;
			}
		}

		// Remember the new buffer and its size.
		stream->_input = buffer;
		stream->_input_size = length;

		goto read; // we have to read additional data - no need to check for it
	}

	if (length > available) // If the available data in the buffer is not enough to satisfy the request
read:
	{
		ssize_t size;

		// Realign buffer data if necessary
		if ((stream->_input_index + length) > stream->_input_size)
		{
			// Move byte by byte because the source and the destination overlap
			size_t i;
			for(i = 0; i < available; ++i)
				stream->_input[i] = stream->_input[i + stream->_input_index];

			stream->_input_index = 0;
			stream->_input_length = available;
		}

		// Read until the buffer contains enough data to satisfy the request
		while (1)
		{
#if defined(TLS)
			if (stream->_tls) size = gnutls_read(stream->_tls, stream->_input + stream->_input_length, stream->_input_size - stream->_input_length);
			else
#endif
				size = read(stream->fd, stream->_input + stream->_input_length, stream->_input_size - stream->_input_length);
			if (size > 0)
			{
				stream->_input_length += size;
				available += size;
				if (available < length) continue;
				else break;
			}
			else if (!size) return ERROR_NETWORK;

			// assert(size < 0);
			// TODO handle all possible errors and handle them properly

#if defined(TLS)
			if (stream->_tls)
			{
				switch (size)
				{
					int status;
				case GNUTLS_E_AGAIN: // TODO ?call timeout(, POLLOUT)
					// Check if there is more data waiting to be read.
					if (status = timeout(stream->fd, POLLIN)) return status;
				case GNUTLS_E_INTERRUPTED:
					continue;

				case GNUTLS_E_REHANDSHAKE:
					// TODO check non-fatal error codes: http://www.gnu.org/software/gnutls/reference/gnutls-gnutls.html#gnutls-handshake
					/*while ((status = gnutls_handshake(session)) < 0)
						if (gnutls_error_is_fatal(status))
							return ERROR; // TODO choose appropriate error*/
					continue;

				default:
					errno = 0; // make sure errno_error() below returns ERROR
				}
			}
#endif

			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				// Check if there is more data waiting to be read.
				int status = timeout(stream->fd, POLLIN);
				if (status) return status;
			}
			else if (errno != EINTR) return errno_error(errno);
		}
	}

	// Set buffer to point to the available data
	buffer->data = stream->_input + stream->_input_index;
	buffer->length = available;
	return 0;
}

void stream_read_flush(struct stream *restrict stream, size_t length)
{
	stream->_input_index += length;

	// Reset length and index position if the buffer holds no data
	if (stream->_input_index == stream->_input_length)
	{
		stream->_input_index = 0;
		stream->_input_length = 0;
		if (stream->_input_size > BUFFER_SIZE_MIN)
		{
			stream->_input = realloc(stream->_input, BUFFER_SIZE_MIN);
			stream->_input_size = BUFFER_SIZE_MIN;
		}
	}
}

// Tries to write data without blocking. Returns number of bytes written or error code on error.
static ssize_t stream_write_internal(struct stream *restrict stream, const char *restrict buffer, size_t size)
{
	ssize_t status;

#if defined(TLS)
	if (stream->_tls)
	{
		status = gnutls_write(stream->_tls, buffer, (stream->_tls_retry ? stream->_tls_retry : ((size > TLS_RECORD) ? TLS_RECORD : size)));
		if (status > 0)
		{
			stream->_tls_retry = 0;
			return status;
		}
		// TODO handle all possible errors and handle them properly
		switch (status)
		{
		case GNUTLS_E_INTERRUPTED:
		case GNUTLS_E_AGAIN:
			if (!stream->_tls_retry) stream->_tls_retry = size;
			return 0;
		default:
#if !defined(OS_WINDOWS)
			return ERROR;
#else
			return -32767;
#endif
		}
	}
#endif

	status = write(stream->fd, buffer, ((size > WRITE_MAX) ? WRITE_MAX : size));
	if (status < 0)
	{
		status = errno_error(errno);
		if (status == ERROR_AGAIN) return 0;
	}
	return status;
}

int stream_write(struct stream *restrict stream, const struct string *buffer)
{
	ssize_t size;
	size_t available;
	size_t index = 0;
#if defined(TLS)
	size_t rest;
#endif

	// If there is buffered data in stream->_output, send it first.
	while (available = stream->_output_length - stream->_output_index)
	{
#if defined(TLS)
		// Send as much data as possible in a single request for TLS unless we must retry the last write attempt.
		// Add more data to the output buffer if the available data is less than the optimal amount.
		if (stream->_tls && !stream->_tls_retry && (available < TLS_RECORD))
		{
			rest = TLS_RECORD - available;

			// If the required amount of data won't fit in the output buffer, move buffer contents at the start of the buffer.
			if (rest < (stream->_output_size - stream->_output_length))
			{
				stream->_output_length -= stream->_output_index;
				memmove(stream->_output, stream->_output + stream->_output_index, stream->_output_length);
				stream->_output_index = 0;
			}

			// If the data is less than the expected packet size, buffer it without sending anything.
			if (rest > buffer->length)
			{
				memcpy(stream->_output + stream->_output_length, buffer->data + index, buffer->length);
				stream->_output_length += buffer->length;
				return 0;
			}

			memcpy(stream->_output + stream->_output_length, buffer->data + index, rest);
			stream->_output_length += rest;
			index += rest;
		}
#endif

		size = stream_write_internal(stream, stream->_output + stream->_output_index, available);
		if (size > 0)
		{
			stream->_output_index += size;
			if (stream->_output_index == stream->_output_length)
			{
				stream->_output_index = 0;
				stream->_output_length = 0;
			}
			continue;
		}
		else if (size) return size;

		// The remaining data can not be written immediately.

		available += buffer->length;
		if (available > BUFFER_SIZE_MAX)
		{
			// The remaining data is too much to buffer it. Wait until more data can be written.
			if (size = timeout(stream->fd, POLLOUT)) return size;
		}
		else
		{
			// Move buffer data at the beginning of the buffer.
			if (stream->_output_index)
			{
				stream->_output_length -= stream->_output_index;
				memmove(stream->_output, stream->_output + stream->_output_index, stream->_output_length);
				stream->_output_index = 0;
			}

			// Buffer the remaining data. Expand the buffer if it's not big enough.
			if (available > stream->_output_size)
			{
				char *new = realloc(stream->_output, available);
				if (!new) return ERROR_MEMORY;
				stream->_output = new;
				stream->_output_size = available;
			}
			memcpy(stream->_output + stream->_output_length, buffer->data, buffer->length);
			stream->_output_length = available;

			return 0;
		}
	}

	// now stream->_output is empty and stream->_output_index == 0

	// Send the data in buffer.
	while (available = buffer->length - index)
	{
#if defined(TLS)
		// Buffer the data instead of sending it if it is less than the optimal size for TLS record.
		if (stream->_tls && (available < TLS_RECORD))
		{
			memcpy(stream->_output, buffer->data + index, available);
			stream->_output_length = available;
			return 0;
		}
#endif

		size = stream_write_internal(stream, buffer->data + index, available);
		if (size > 0)
		{
			index += size;
			continue;
		}
		else if (size) return size;

		// The remaining data can not be written immediately.

		if (available > BUFFER_SIZE_MAX)
		{
			// The remaining data is too much to buffer it. Wait until more data can be written.
			if (size = timeout(stream->fd, POLLOUT)) return size;
		}
		else
		{
			// Buffer the remaining data. Expand the buffer if it's not big enough.
			if (available > stream->_output_size)
			{
				char *new = realloc(stream->_output, available);
				if (!new) return ERROR_MEMORY;
				stream->_output = new;
				stream->_output_size = available;
			}
			memcpy(stream->_output, buffer->data + index, available);
			stream->_output_length = available;
			return 0;
		}
	}

	return 0;
}

#include "log.h"

int stream_write_flush(struct stream *restrict stream)
{
	ssize_t size;
	size_t available;

	while (available = stream->_output_length - stream->_output_index)
	{
		size = stream_write_internal(stream, stream->_output + stream->_output_index, available);
		if (size > 0)
		{
			stream->_output_index += size;
			continue;
		}
		else if (size) return size;

		// Wait until more data can be written.
		if (size = timeout(stream->fd, POLLOUT)) return size;
	}

	// Set output buffer as empty. Shrink it if necessary.
	stream->_output_index = 0;
	stream->_output_length = 0;
	if (stream->_output_size > BUFFER_SIZE_MIN)
	{
		stream->_output = realloc(stream->_output, BUFFER_SIZE_MIN);
		stream->_output_size = BUFFER_SIZE_MIN;
	}

	return 0;
}
