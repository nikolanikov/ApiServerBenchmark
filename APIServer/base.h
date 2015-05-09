
// System resources are not sufficient to handle the request.
#define ERROR_MEMORY				-1

// Invalid input data.
#define ERROR_INPUT					-2

// Request requires access rights that are not available.
#define ERROR_ACCESS				-3

// Entity that is required for the operation is missing.
#define ERROR_MISSING				-4

// Unable to create a necessary entity because it exists.
#define ERROR_EXIST					-5

// Filement filesystem internal error.
#define ERROR_EVFS					-6

// Temporary condition caused error. TODO maybe rename this to ERROR_BUSY
#define ERROR_AGAIN					-7

// Unsupported feature is required to satisfy the request.
#define ERROR_UNSUPPORTED			-8 /* TODO maybe rename to ERROR_SUPPORT */

// Read error.
#define ERROR_READ					-9

// Write error.
#define ERROR_WRITE					-10

// Action was cancelled.
#define ERROR_CANCEL				-11

// An asynchronous operation is now in progress.
#define ERROR_PROGRESS				-12

// Unable to resolve domain.
#define ERROR_RESOLVE				-13

// Network operation failed.
#define ERROR_NETWORK				-14

// An upstream server returned invalid response.
#define ERROR_GATEWAY				-15

// Invalid session.
#define ERROR_SESSION				-16

// Unknown error.
#define ERROR						-32767

////////////////

// String literal. Contains length and pointer to the data. The data is usually NUL-terminated so that it can be passed to standard functions without modification.
struct string
{
    char *data;
    size_t length;
};

// Generates string literal from data and length. If no length is passed, it assumes that string data is static array and determines its length with sizeof.
// Examples:
//  struct string name = string(name_data, name_length);
//  struct string key = string("uuid");
#define string_(data, length, ...) (struct string){(data), (length)}
#define string(...) string_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

#define string_equal(s0, s1) (((s0)->length == (s1)->length) && !memcmp((s0)->data, (s1)->data, (s0)->length))

/* Vector */

#include <stdbool.h>

struct vector
{
    void **data;
    size_t length, size;
};

#define VECTOR_SIZE_BASE 4

bool vector_init(struct vector *restrict v, size_t size);
#define vector_get(vector, index) ((vector)->data[index])
bool vector_add(struct vector *restrict v, void *value);
#define vector_term(v) (free(((struct vector *)(v))->data))

/* Dictionary */

#define DICT_SIZE_BASE 16

struct dict
{
    struct dict_item
    {
        size_t key_size;
        const char *key_data;
        void *value;
        struct dict_item *_next;
    } **items;
    size_t count, size;
};

struct dict_iterator
{
    size_t index;
    struct dict_item *item;
};

// Initializes dictionary iterator and returns the first item
const struct dict_item *dict_first(struct dict_iterator *restrict it, const struct dict *d);
// Returns next item in a dictionary iterator
const struct dict_item *dict_next(struct dict_iterator *restrict it, const struct dict *d);

// WARNING: size must be a power of 2
bool dict_init(struct dict *restrict dict, size_t size);

int dict_set(struct dict *restrict dict, const struct string *key, void *value, void **result);
#define dict_add(dict, key, value) dict_set((dict), (key), (value), 0)

void *dict_get(const struct dict *dict, const struct string *key);

void *dict_remove(struct dict *restrict dict, const struct string *key);

void dict_term(struct dict *restrict dict);
void dict_term_custom(struct dict *restrict dict, void (*custom)(void *));

////////////////

static inline void *alloc(size_t size)
{
	void *buffer = malloc(size);
	if (!buffer) abort();
	return buffer;
}

static inline void *realloc_(void *old, size_t size)
{
	void *new = (realloc)(old, size);
	if (!new)
	{
		free(old);
		abort();
	}
	return new;
}
#define realloc(buffer, size) realloc_((buffer), (size))

static inline void *dupalloc(void *old, size_t size)
{
	void *new = malloc(size);
	if (!new) abort();
	memcpy(new, old, size);
	return new;
}

////////////////

typedef struct
{
	size_t size;
	unsigned char data[];
} bytes_t;

#define bytes_t(n) struct \
	{ \
		size_t size; \
		char data[n]; \
	}

#define bytes(value) {sizeof(value) - 1, value}

#define bytes_define(variable, value) bytes_t(sizeof(value) - 1) variable = bytes(value)

// TODO ? make static assert for offsetof(..., data) as this ensures the struct is compatible with bytes_t
#define bytes_p(s) (bytes_t *)&( \
		struct \
		{ \
			size_t size; \
			char data[sizeof(s) - 1]; \
		} \
	){sizeof(s) - 1, (s)}
