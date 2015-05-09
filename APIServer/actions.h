#include "http.h"
#include "http_parse.h"
#include "http_response.h"

#define ACTIONS \
    {.name = {.data = "article.get_version", .length = 19}, .handler = &article_get_version},\
    {.name = {.data = "example.hello_world", .length = 19}, .handler = &example_hello_world},

int article_get_version(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options);
int example_hello_world(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options);
