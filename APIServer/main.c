#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "base.h"
#include "log.h"
#include "format.h"
#include "stream.h"
#include "json.h"
#include "server.h"
#include "http.h"
#include "http_parse.h"
#include "http_response.h"

#define LISTEN_MAX 10

#define STATUS_BUFFER 64

#define PORT_HTTP 8080

struct connection
{
	enum {Listen = 1, Parse, ResponseStatic, ResponseDynamic} type;
	struct http_context context;
	struct resources resources;
	size_t thread;
	time_t activity;
};

struct thread_pool
{
	pthread_t thread_id;
	struct io
	{
		int request[2];
		int response[2];
	} io;
	unsigned busy;
};

#define THREAD_POOL_SIZE 4

struct string SERVER = {"test/1.0", 8};

static const struct string key_connection = {"Connection", 10}, value_close = {"close", 5};

static int init(void)
{
#if !defined(DEBUG)
	int pid = fork();
	if (pid < 0) return -1;
	if (pid) _exit(0); // stop parent process

	if (setsid() < 0) return -1;
#endif

	if (chdir("/") < 0) return -1;

	struct sigaction action = {
		.sa_handler = SIG_IGN,
		.sa_mask = 0,
		.sa_flags = 0
	};
	sigaction(SIGPIPE, &action, 0); // never fails when used properly

	umask(0);

	// TODO: this is not the best solution but it works for most cases
	// TODO: check for errors?
	//close(0);
	//close(1);
	//close(2);

	// TODO: check for errors?
	//open("/dev/null", O_RDONLY);
	//open("/dev/null", O_WRONLY);
	//dup(1);

	return 0;
}

static void response_init(struct http_response *restrict response)
{
	response->headers_end = response->headers;
	response->content_encoding = -1;
	response->ranges = 0;
}

static void response_term(struct http_response *restrict response)
{
	if (response->content_encoding < 0) return; // no response initialized
	response->content_encoding = -1;
	free(response->ranges);
}

// Releases control of the connection-related resources.
// Uses a pipe to tell the dispatcher what to do with the socket.
/*static void connection_release(int control, int status)
{
	if (control >= 0)
	{
		write(control, &status, sizeof(status));
		close(control);
	}
}*/

static void *server_serve(void *argument)
{
	struct connection *connection = argument;

	struct http_request *request = &connection->context.request;
	struct http_response response;
	struct string key, value;
	int status;
	bool last;

	// Remember to terminate the connection if the client specified so.
	{
		struct string *connection = dict_get(&request->headers, &key_connection);
		last = (connection && string_equal(connection, &value_close));
	}

	response_init(&response);

	// Assume that response_header_add() will always succeed before the response handler is called.

	// Server - Server name and version.
	key = string("Server");
	response_header_add(&response, &key, &SERVER);

	// Allow cross-origin requests.
	key = string("origin");
	if (dict_get(&request->headers, &key))
	{
		// TODO: maybe allow only some domains as origin. is origin always in the same format as allow-origin ?
		key = string("Access-Control-Allow-Origin");
		value = string("*");
		response_header_add(&response, &key, &value);

		// TODO is this okay
		key = string("Access-Control-Expose-Headers");
		value = string("Server, UUID");
		response_header_add(&response, &key, &value);
	}

	// TODO: change this to do stuff properly
	if (request->method == METHOD_OPTIONS)
	{
		// TODO: Access-Control-Request-Headers
		key = string("Access-Control-Allow-Headers");
		value = string("Cache-Control, X-Requested-With, Filename, Filesize, Content-Type, Content-Length, Authorization, Range");
		response_header_add(&response, &key, &value);

		// TODO is this okay
		key = string("Access-Control-Expose-Headers");
		value = string("Server, UUID");
		response_header_add(&response, &key, &value);

		// TODO: Access-Control-Request-Method
		key = string("Access-Control-Allow-Methods");
		value = string("GET, POST, OPTIONS, PUT, DELETE, SUBSCRIBE, NOTIFY");
		response_header_add(&response, &key, &value);

		status = 0;
		response.code = OK;
	}
	else
	{
		// Parse request URI and call appropriate handler.
		if (response.code = http_parse_uri(request)) goto finally;
		else
		{
			// If there is a query, generate dynamic content and send it. Otherwise send static content.
			response.code = InternalServerError; // default response code
			if (request->query)
			{
				status = handler_dynamic(request, &response, &connection->resources);
				json_free(request->query);
			}
			else status = handler_static(request, &response, &connection->resources);
			free(request->path.data);

			// Close the connection on error with a request containing body.
			// TODO is this okay?
			if (status && ((request->method == METHOD_POST) || (request->method == METHOD_PUT)))
			{
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
			}

			// TODO this is a fix for old actions that return HTTP status codes
			if (status > 0)
			{
				response.code = status;
				goto finally;
			}

			switch (status)
			{
			case ERROR_CANCEL:
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
			case ERROR_PROGRESS:
				response.code = OK;
			case 0:
				break;

			case ERROR_ACCESS:
			case ERROR_SESSION:
				response.code = Forbidden;
				break;

			case ERROR_INPUT:
			case ERROR_EXIST:
			case ERROR_MISSING:
			case ERROR_READ:
			case ERROR_WRITE:
			case ERROR_RESOLVE:
				response.code = NotFound;
				break;

			case ERROR_EVFS:
			default:
				response.code = InternalServerError;
				break;

			case ERROR_UNSUPPORTED:
				response.code = NotImplemented;
				break;
			
			case ERROR_AGAIN:
				response.code = ServiceUnavailable;
				break;

			case ERROR_GATEWAY:
				if (!response_header_add(&response, &key_connection, &value_close)) goto error;
				last = true;
				response.code = BadGateway;
				break;

			case ERROR_MEMORY:
				response.code = ServiceUnavailable;
				// TODO send response
			case ERROR_NETWORK:
				goto error; // not possible to send response
			}
		}
	}

finally:

	if (status == ERROR_PROGRESS)
	{
		response_term(&response);
		return 0;
	}

	// Send default response if specified but only if none is sent until now.
	if (response.content_encoding < 0)
		last |= !response_headers_send(&connection->resources.stream, request, &response, 0);

	if (last) status = 1;

	if (0)
	{
error:
		status = -1;
	}

	response_term(&response);
	//connection_release(connection->control, status); // TODO should this be in a function?

	return 0;
}

static void *worker(void *argument)
{
	struct io *io = argument;

	while (1)
	{
		void *input, *output;

		read(io->request[0], &input, sizeof(input));
		// assert(read() returned sizeof(connection));
		output = server_serve(input);
		write(io->response[1], &output, sizeof(output));
	}

	return 0;
}

// Listen for incoming HTTP connections.
void server_listen(void *storage)
{
	size_t connections_count = 0, connections_size;
	struct pollfd *wait;
	struct connection **connections, *connection;

	struct thread_pool pool[THREAD_POOL_SIZE];
	size_t thread, thread_next = 0;
	size_t pool_free[THREAD_POOL_SIZE], pool_free_count;

	size_t i;
	struct sockaddr_in address;
	socklen_t address_len;
	pthread_t thread_id;

	// Start the thread pool.
	for(i = 0; i < THREAD_POOL_SIZE; ++i)
	{
		pipe(pool[i].io.request);
		pipe(pool[i].io.response);

		pool[i].busy = 0;

		pthread_create(&pool[i].thread_id, 0, &worker, (void *)&pool[i].io);
		pthread_detach(thread_id);

		pool_free[i] = i;
	}
	pool_free_count = THREAD_POOL_SIZE;

	// Allocate memory for connection data.
	connections_size = 8; // TODO change this
	wait = malloc(connections_size * sizeof(*wait));
	connections = malloc(connections_size * sizeof(*connections));
	if (!wait || !connections)
	{
		error(logs("Unable to allocate memory"));
		goto error;
	}

	// Create listening socket.
	{
		int value = 1;
		size_t i = 0;

		wait[i].fd = socket(PF_INET, SOCK_STREAM, 0);
		if (wait[i].fd < 0)
		{
			error(logs("Unable to create socket"));
			goto error;
		}
		wait[i].events = POLLIN;
		wait[i].revents = 0;

		// disable TCP time_wait
		// TODO should I do this?
		setsockopt(wait[i].fd, SOL_SOCKET, SO_REUSEADDR, (void *)&value, sizeof(value)); // TODO can this fail

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(PORT_HTTP);
		if (bind(wait[i].fd, (struct sockaddr *)&address, sizeof(address)))
		{
			error(logs("Unable to bind to port "), logi(PORT_HTTP));
			goto error;
		}
		if (listen(wait[i].fd, LISTEN_MAX))
		{
			error(logs("Listen error"));
			goto error;
		}

		connections[i] = malloc(sizeof(**connections));
		if (!connections[i])
		{
			error(logs("Unable to allocate memory"));
			goto error;
		}

		connections[i]->type = Listen;
		// TODO set other fields

		connections_count = 1;
	}

	int client;
	int status;
	size_t poll_count;
	time_t now;

	// TODO connections elements may be changed by another thread; think about this (are there caching problems?)
	// http://stackoverflow.com/questions/26097773/non-simultaneous-memory-use-from-multiple-threads-caching

	// TODO add one more listening socket for https

	// Start an event loop to handle the connections.
	while (1)
	{
		if (poll(wait, connections_count, -1) < 0) continue; // TODO set timeout

		now = time(0);

		poll_count = connections_count;
		for(i = 0; i < poll_count; ++i)
		{
			if (wait[i].revents & POLLIN)
			{
				wait[i].revents = 0;

				switch (connections[i]->type)
				{
				case Listen:
					// A client has connected to the server. Accept the connection and prepare for parsing.

					// Make sure there is enough allocated memory to store connection data.
					if (connections_count == connections_size)
					{
						void *p;

						connections_size *= 2;

						p = realloc(wait, connections_size * sizeof(*wait));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						wait = p;

						p = realloc(connections, connections_size * sizeof(*connections));
						if (!p)
						{
							error(logs("Unable to allocate memory"));
							goto error;
						}
						connections = p;
					}

					connections[connections_count] = malloc(sizeof(**connections));
					if (!connections[connections_count])
					{
						error(logs("Unable to allocate memory"));
						goto error;
					}
					connection = connections[connections_count];
					memset(&connection->resources, 0, sizeof(connection->resources));

					address_len = sizeof(connection->resources.address);
					if ((client = accept(wait[i].fd, (struct sockaddr *)&connection->resources.address, &address_len)) < 0)
						continue;
					http_open(client);
					if (stream_init(&connection->resources.stream, client))
					{
						warning(logs("Unable to initialize stream"));
						http_close(client);
						continue;
					}
					connection->type = Parse;
					connection->activity = now;
					http_parse_init(&connection->context); // TODO error check

					wait[connections_count].fd = client;
					wait[connections_count].events = POLLIN;
					wait[connections_count].revents = 0;

					connections_count += 1;
					break;

				case Parse:
					connection = connections[i];

					// Request data received. Try parsing it.

					status = http_parse(&connection->context, &connection->resources.stream);
					if (!status)
					{
						// Request parsed successfully.

						// Check if host header is specified.
						struct string name = string("host");
						connection->context.request.hostname = dict_get(&connection->context.request.headers, &name);
						if (!connection->context.request.hostname)
						{
							// TODO send BadRequest
							status = BadRequest;
							goto term;
						}

						// Use a separate thread to handle the request and send response.
						// Create a pipe for communication between the two threads.

						connection->resources.storage = storage;

						// TODO get free thread faster
						//if (pool_free_count) thread = pool_free[--pool_free_count];
						//else
						//{
						//	thread = thread_next;
						//	thread_next = (thread_next + 1) % THREAD_POOL_SIZE;
						//}

						for(thread = 0; thread < THREAD_POOL_SIZE; ++thread)
							if (!pool[thread].busy)
								break;

						if (thread == THREAD_POOL_SIZE)
						{
							thread = thread_next;
							thread_next = (thread_next + 1) % THREAD_POOL_SIZE;
						}

						wait[i].fd = pool[thread].io.response[0];

						/*
						// Create a pipe that will be polled to determine the response status.
						int control[2];
						if (pipe(control))
						{
							error(logs("Unable to create pipe"));
							status = ERROR_MEMORY;
							goto term;
						}
						wait[i].fd = control[0];
						connection->control = control[1];
						*/

						connection->thread = thread;
						connection->type = ResponseDynamic;
						connection->activity = now;

						pool[thread].busy += 1;
						write(pool[thread].io.request[1], (void *)&connection, sizeof(void *));

						// TODO determine whether the request is static or dynamic
						// TODO handle static requests separately

						/*
						pthread_create(&thread_id, 0, &server_serve, connection);
						pthread_detach(thread_id);
						*/
					}
					else if (status != ERROR_AGAIN) goto term;
					else connection->activity = now;
					break;

				/*case ResponseStatic:
					break;*/

				case ResponseDynamic:
					connection = connections[i];

					thread = connection->thread;

					{
						void *response;
						read(pool[thread].io.response[0], &response, sizeof(response));
						// assert(read() returned sizeof(response));
						status = (response != 0);
					}

					pool[thread].busy -= 1;

					//if (!pool[thread].busy)
					//	pool_free[pool_free_count++] = thread;

					wait[i].fd = connection->resources.stream.fd;

					if (status) goto term;
					else
					{
						connection->type = Parse;
						connection->activity = now;
					}

					http_parse_term(&connection->context); // TODO race condition here?
					http_parse_init(&connection->context); // TODO error check

					break;
				}
			}
			else if (wait[i].revents)
			{
				if (connections[i]->type == ResponseDynamic)
				{
					close(wait[i].fd);
					wait[i].fd = connections[i]->resources.stream.fd;
				}
				// else assert(connections[i]->type == Parse);

				status = -1;
				goto term; // TODO race condition here?
			}
			else if ((connections[i]->type == Parse) && ((now - connections[i]->activity) > (TIMEOUT / 1000)))
			{
				status = ERROR_AGAIN;
				goto term;
			}

			continue;

term:
			http_parse_term(&connections[i]->context);
			stream_term(&connections[i]->resources.stream);
			if (status >= 0) http_close(connections[i]->resources.stream.fd);
			else close(connections[i]->resources.stream.fd); // close with RST
			free(connections[i]);

			// Fill the entry freed by the terminated connection.
			// Make sure the moved entry is inspected (if included in poll_count).
			if (i != --connections_count)
			{
				wait[i] = wait[connections_count];
				connections[i] = connections[connections_count];
			}
			if (connections_count < poll_count)
			{
				poll_count -= 1;
				i -= 1;
			}

			// Shrink the memory allocated for connection data when appropriate to save memory.
			/*if (((connections_count * 4) <= connections_size) && (8 < connections_size)) // TODO remove this 8
			{
				connections_size /= 2;
				wait = realloc(wait, connections_size * sizeof(*wait));
				connections = realloc(connections, connections_size * sizeof(*connections));
			}*/
		}
	}

error:
	free(wait);
	for(i = 0; i < connections_count; ++i)
		free(connections[i]);
	free(connections);
}

int main(void)
{
	init();

	server_listen(0);

	return 0;
}
