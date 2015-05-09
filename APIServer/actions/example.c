#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "../base.h"
#include "../stream.h"
#include "../server.h"
#include "../actions.h"

int example_hello_world(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	struct string entity = string("Hello world!\n");
	response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, entity.length))
		return -1;
	response_entity_send(&resources->stream, response, entity.data, entity.length);
	return 0;
}
