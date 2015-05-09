// WARNING: Works only on POSIX-compatible systems

#include "format.h"
//#include "test/format.h"

// TODO: fix dependency on libc
// TODO: possible future improvements:
//  format_sint			puts + sign for positive numbers
//  format_real
//  custom digits for format_hex and format_base64
//  length functions
//  ? memory allocation functions
/*
//  char *format_base64(char *restrict buffer, const uint8_t *restrict bytes, size_t length, uint32_t *state, char alphabet[65])

format_base64_step()
format_base64_flush()

char *destination
uint8_t *source
size_t length
char alphabet[]
uint32_t *state
*/
// TODO implement format_real in order to use it in json_dump to increase the speed
// TODO: optimize for bases 8, 10, 16

static const unsigned char digits[64] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_", padding = '.';

uint32_t format_uint_length(uintmax_t number, uint8_t base)
{
	uint32_t length = 1;
	while (number /= base) ++length;
	return length;
}

uint8_t *format_uint(uint8_t *buffer, uintmax_t number, uint8_t base)
{
	uint8_t temp[sizeof(number) * 8]; // wide enough for any value of number in base-2
	uint8_t *position = temp + sizeof(temp);

	size_t length;

	// Write the result string in temporary memory.
	do *--position = digits[(size_t)(number % base)];
	while (number /= base);

	// Copy the result string to buffer.
	length = sizeof(temp) - (position - temp);
	memcpy(buffer, position, length);
	return buffer + length;
}

uint8_t *format_uint_pad(uint8_t *buffer, uintmax_t number, uint8_t base, uint32_t length, uint8_t fill)
{
	uint8_t *position = buffer + length;

	// Write the result string at the end of the buffer.
	do *--position = digits[(size_t)(number % base)];
	while (number /= base);

	// Fill the remaining space at the beginning of the buffer with padding.
	if (position > buffer) memset(buffer, fill, position - buffer);

	return buffer + length;
}

uint32_t format_int_length(intmax_t number, uint8_t base)
{
	uint32_t length = 1;
	if (number < 0) ++length;
	while (number /= base) ++length;
	return length;
}

// TODO given the guarantees provided by int64_t, this can be simplified by converting number to uint64_t with -(uint64_t)number
uint8_t *format_int(uint8_t *buffer, intmax_t number, uint8_t base)
{
	uint8_t temp[sizeof(number) * 8]; // wide enough for any value of number in base-2 without the sign
	uint8_t *position = temp + sizeof(temp);

	int digit;
	size_t length;

	// In 2's complement INT64_MIN can not be safely negated.
	// Extract the least significant digit from number to ensure number is small enough to be negated.
	digit = number % base;
	if (number < 0)
	{
		digit = -digit;
		*buffer++ = '-'; // minus

		number /= base;
		number = -number; // negate number and treat it as uint from now on
	}
	else number /= base;

	// Write the result string in temporary memory.
	while (1)
	{
		*--position = digits[(size_t)digit];
		if (!number) break; // no more digits
		digit = number % base;
		number /= base;
	}

	// Copy the result string to buffer.
	length = sizeof(temp) - (position - temp);
	memcpy(buffer, position, length);
	return buffer + length;
}

uint8_t *format_int_pad(uint8_t *buffer, intmax_t number, uint8_t base, uint32_t length, uint8_t fill)
{
	uint8_t *position = buffer + length;
	int negative = 0;

	int digit;

	// In 2's complement INT64_MIN can not be safely negated.
	// Extract the least significant digit from number to ensure number is small enough to be negated.
	digit = number % base;
	if (number < 0)
	{
		digit = -digit;
		negative = 1;

		number /= base;
		number = -number; // negate number and treat it as uint from now on
	}
	else number /= base;

	// Write the result string in temporary memory.
	while (1)
	{
		*--position = digits[(size_t)digit];
		if (!number) break; // no more digits
		digit = number % base;
		number /= base;
	}

	// Fill the remaining space at the beginning of the buffer with padding.
	if (negative) *--position = '-'; // minus
	if (position > buffer) memset(buffer, fill, position - buffer);

	return buffer + length;
}

char *format_hex(char *restrict buffer, const uint8_t *restrict bytes, size_t length)
{
	size_t i;
	for(i = 0; i < length; ++i)
	{
		*buffer++ = digits[(bytes[i] >> 4) & 0x0f];
		*buffer++ = digits[bytes[i] & 0x0f];
	}
	return buffer;
}

char *format_base64(char *restrict buffer, const uint8_t *restrict bytes, size_t length)
{
	uint32_t block = 0;
	char *start = buffer;
	size_t index;
	size_t remaining; // TODO the name of this variable is not consistent with its usage

	for(index = 2; index < length; index += 3)
	{
		// Store block of 24 bits in block.
		block = (bytes[index - 2] << 16) | (bytes[index - 1] << 8) | bytes[index];

		*start++ = digits[block >> 18];
		*start++ = digits[(block >> 12) & 0x3f];
		*start++ = digits[(block >> 6) & 0x3f];
		*start++ = digits[block & 0x3f];
	}

	// Encode the remaining bytes.
	block = 0;
	switch (remaining = index - length)
	{
	case 2: // no more bytes
		return start;

	case 0: // 2 bytes remaining
		block |= (bytes[index - 1] << 8);
		start[2] = digits[(block >> 6) & 0x3f];
	case 1: // 1 byte remaining
		block |= (bytes[index - 2] << 16);
		start[0] = digits[block >> 18];
		start[1] = digits[(block >> 12) & 0x3f];
		return start + 3 - remaining;
	}
}

/////////////////////

// TODO: fix this
size_t hex2bin(unsigned char *restrict dest, const unsigned char *src, size_t length)
{
	static const char digits[256] = {
		['0'] =  0, ['1'] =  1, ['2'] =  2, ['3'] =  3,
		['4'] =  4, ['5'] =  5, ['6'] =  6, ['7'] =  7,
		['8'] =  8, ['9'] =  9, ['a'] = 10, ['b'] = 11,
		['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15};
	size_t index;
	for(index = 0; (index * 2) < length; ++index)
		dest[index] = (digits[(int)src[index * 2]] << 4) | digits[(int)src[index * 2 + 1]];
	return index;
}

static const unsigned char base64_int[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,   62,    0, 0xff,
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,   49,   50,
	  51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61, 0xff, 0xff, 0xff, 0xff,   63,
	0xff,   10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
	  25,   26,   27,   28,   29,   30,   31,   32,   33,   34,   35, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const unsigned char BASE64_PADDING = '.';

// TODO move this to parse.c
// TODO fix this function
size_t parse_base64_length(const unsigned char *restrict data, size_t length)
{
	if (!length || (length % 4)) return 0;
	//if (length % 4) return ((length & ~0x3) / 4) * 3 + (length % 4) - 1;
	else return (length / 4) * 3 - (data[length - 1] == BASE64_PADDING) - (data[length - 2] == BASE64_PADDING);
}

// TODO move this to parse.c
// WARNING: This implementation doesn't handle = properly.
size_t parse_base64(const unsigned char *src, unsigned char *restrict dest, size_t length)
{
	uint32_t block = 0;
	unsigned char value;
	unsigned char phase = 0;

	if (!length || (length % 4)) return 0;

	const unsigned char *end = src + length;
	length = (length / 4) * 3;
	while (1)
	{
		value = base64_int[src[0]];
		if (value > 63) return -1;
		block = (block << 6) | value;

		++src;
		if (++phase == 4)
		{
			dest[0] = block >> 16;
			if (src[-2] == BASE64_PADDING) return length - 2;
			dest[1] = (block >> 8) & 0xff;
			if (src[-1] == BASE64_PADDING) return length - 1;
			dest[2] = block & 0xff;
			if (src == end) return length;

			dest += 3;
			block = 0;
			phase = 0;
		}
	}
}

/*#include <unistd.h>
int main(void)
{
	static char buff[4096];
	write(1, buff, format(buff, uint(15), int(-43, 16, 4), uint(1337, 10, 16, '0'), str(" <\n", 3), bin("\x7f\x2c", 2), str("\n", 1)) - buff);
	//write(1, buff, format(buff, uint(15), int(-43, 16, 4), int(6), uint(1337, 10, 32, '0'), int(-20)) - buff);
}*/
