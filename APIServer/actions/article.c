#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "../base.h"
#include "../stream.h"
#include "../format.h"
#include "../server.h"
#include "../storage.h"
#include "../actions.h"

int article_get_version(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
	int status;

	struct file_info *file_info = storage_get(&request->path);

	char buffer[64], *position; // TODO change this
	position = format_bytes(buffer, "{\"version\": ", sizeof("{\"version\": ") - 1);
	position = format_uint(position, file_info->version, 10);
	position = format_bytes(position, "}", sizeof("}") - 1);

	response->code = OK;
	if (!response_headers_send(&resources->stream, request, response, file_info->size))
		return -1;

	if (response->content_encoding) // if response body is required
		status = response_entity_send(&resources->stream, response, buffer, position - buffer);

	storage_release(file_info);

	return status;
}
