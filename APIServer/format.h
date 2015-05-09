#include <stddef.h>
#include <stdint.h>
#include <string.h>

// TODO don't use memset and memcpy

#define _VA_ARGS_EMPTY(...) (sizeof(#__VA_ARGS__) == 1)

// Supported values for base are the integers in the interval [2, 36].

uint8_t *format_uint(uint8_t *buffer, uintmax_t number, uint8_t base);
uint8_t *format_uint_pad(uint8_t *buffer, uintmax_t number, uint8_t base, uint32_t length, uint8_t fill);
uint32_t format_uint_length(uintmax_t number, uint8_t base);

uint8_t *format_int(uint8_t *buffer, intmax_t number, uint8_t base);
uint8_t *format_int_pad(uint8_t *buffer, intmax_t number, uint8_t base, uint32_t length, uint8_t fill);
uint32_t format_int_length(intmax_t number, uint8_t base);

static inline char *format_byte_one(char *restrict buffer, uint8_t byte) // TODO is this necessary
{
	*buffer = byte;
	return buffer + sizeof(byte);
}
static inline char *format_byte_many(char *restrict buffer, uint8_t byte, size_t size)
{
	memset(buffer, byte, size);
	return buffer + size;
}
#define format_byte(buffer, byte, ...) (_VA_ARGS_EMPTY(__VA_ARGS__) ? format_byte_one((buffer), (byte)) : format_byte_many((buffer), (byte), __VA_ARGS__))

// TODO mempcpy does exactly what format_bytes is supposed to do but is GNU extension; could I use it?
static inline char *format_bytes(char *restrict buffer, const uint8_t *restrict bytes, size_t size)
{
	memcpy(buffer, bytes, size);
	return buffer + size;
}

char *format_hex(char *restrict buffer, const uint8_t *restrict bin, size_t length);
#define format_hex_length(bin, length) ((length) * 2)

char *format_base64(char *restrict buffer, const uint8_t *restrict bin, size_t length);

// TODO: this should not be here
size_t hex2bin(unsigned char *restrict dest, const unsigned char *src, size_t length);

size_t parse_base64_length(const unsigned char *restrict data, size_t length);
size_t parse_base64(const unsigned char *src, unsigned char *restrict dest, size_t length);

// Complex macro magic that enables beautiful syntax for chain format_* calls.

#define empty()
#define defer(...) __VA_ARGS__ empty()
#define expand(...) __VA_ARGS__

#define CALL(func, ...) func(__VA_ARGS__)
#define FIRST(f, ...) f
#define FIRST_() FIRST

#define expand_int(...) __VA_ARGS__
#define expand_uint(...) __VA_ARGS__
#define expand_str(...) __VA_ARGS__
#define expand_bin(...) __VA_ARGS__
#define expand_base64(...) __VA_ARGS__
#define expand_final() 0

#define format_() format_internal

#define _int(...) format_
#define _uint(...) format_
#define _str(...) format_
#define _bin(...) format_
#define _base64(...) format_
#define _final() FIRST_

#define name_int(...) format_int
#define name_uint(...) format_uint
#define name_str(...) format_bytes
#define name_bin(...) format_hex
#define name_base64(...) format_base64
#define name_final() FIRST

#define format_internal(buffer, fmt, ...) defer(_##fmt)()(CALL(name_##fmt, buffer, expand_##fmt), __VA_ARGS__)

// WARNING: temporary solution for the problem that each defer must be expanded
//  As a result, the effective number of arguments of format is currently limited to 63.
#define expand4(arg) expand(expand(expand(expand(arg))))
#define expand16(arg) expand4(expand4(expand4(expand4(arg))))
#define expand64(arg) expand16(expand16(expand16(expand16(arg))))

#if !defined(OS_IOS)
# define format(...) expand64(format_internal(__VA_ARGS__, final(), 0))
#else
# define format_ios(...) expand64(format_internal(__VA_ARGS__, final(), 0))
#endif
