// http://fossil.wanderinghorse.net/repos/cson/index.cgi/index

#ifndef JSON_H
# define JSON_H

# define JSON_DEPTH_MAX 7

# define NONE 0
# define BOOLEAN 1
# define INTEGER 2
# define REAL 3
# define STRING 4
# define ARRAY 5
# define OBJECT 6

// WARNING: This code assumes that array_node is the largest member of the union.
union json
{
	bool boolean;
	int64_t integer;
	double real;
	struct string string_node;
	struct vector array_node;
	struct dict *object;
	unsigned char _type[sizeof(struct vector) + 1]; // the last array element stores type
};

// TODO using _type in this way is not standard (setting one union field and accessing another is undefined behavior)

// WARNING: This code assumes that array_node is the largest member of the union.
# define json_type(data) (((union json *)data)->_type[sizeof(struct vector)])

union json *json_parse(const struct string *json); // TODO split the arguments

ssize_t json_length_string(const char *restrict data, size_t size);
ssize_t json_length(const union json *restrict json);

char *json_dump_string(unsigned char *restrict dest, const unsigned char *restrict src, size_t size);
char *json_dump(char *restrict result, const union json *restrict json);

struct string *json_serialize(const union json *json); // TODO deprecated

union json *json_none(void);
union json *json_boolean(bool value);
union json *json_integer(long long value);
union json *json_real(double value);
union json *json_string(const char *data, size_t length);
union json *json_array(void);
union json *json_object(void);

union json *json_array_insert(union json *restrict container, union json *restrict value);
union json *json_object_insert(union json *restrict container, const struct string *restrict key, union json *restrict value);

union json *json_clone(const union json *json);

void json_free(union json *restrict json);

// TODO these are deprecated old-API functions; remove them
int json_array_insert_old(union json *restrict parent, union json *restrict child);
union json *json_object_old(bool is_null);
int json_object_insert_old(union json *restrict parent, const struct string *key, union json *restrict value);
union json *json_string_old(const struct string *value); // TODO this should take 2 arguments

#endif /* JSON_H */
