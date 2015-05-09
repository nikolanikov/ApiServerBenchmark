#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "base.h"

bool vector_init(struct vector *restrict v, size_t size)
{
	v->length = 0;
	v->size = size;

	v->data = malloc(sizeof(void *) * size);
	if (!v->data) return 0;
	return v;
}

bool vector_add(struct vector *restrict v, void *value)
{
	if (v->length == v->size)
	{
		void **buffer;
		v->size *= 2;
		buffer = realloc(v->data, sizeof(void *) * v->size);
		if (!buffer) return 0; // not enough memory; operation canceled
		v->data = buffer;
	}
	return (v->data[v->length++] = value);
}
