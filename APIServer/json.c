/*
Copyright (c) 2005 JSON.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include "base.h"
#include "arch.h"
#include "format.h"
#include "json.h"

static struct string *string_init(struct string *restrict s, const char *data, size_t length)
{
    s->data = malloc(sizeof(char) * (length + 1));
    if (!s->data) return 0;
    memcpy(s->data, data, length);
    s->data[length] = 0;
    s->length = length;
    return s;
}

/* Windows DLL stuff */
#ifdef JSON_PARSER_DLL
#   ifdef _MSC_VER
#	    ifdef JSON_PARSER_DLL_EXPORTS
#		    define JSON_PARSER_DLL_API __declspec(dllexport)
#	    else
#		    define JSON_PARSER_DLL_API __declspec(dllimport)
#       endif
#   else
#	    define JSON_PARSER_DLL_API 
#   endif
#else
#	define JSON_PARSER_DLL_API 
#endif

/* Determine the integer type use to parse non-floating point numbers */
#if !defined(OS_WINDOWS)
	typedef long long JSON_int_t;
	#define JSON_PARSER_INTEGER_SSCANF_TOKEN "%lld"
	#define JSON_PARSER_INTEGER_SPRINTF_TOKEN "%lld"
#else
	typedef int64_t JSON_int_t;
	#define JSON_PARSER_INTEGER_SSCANF_TOKEN "%lld"
	#define JSON_PARSER_INTEGER_SPRINTF_TOKEN "%lld"
#endif

typedef enum 
{
    JSON_E_NONE = 0,
    JSON_E_INVALID_CHAR,
    JSON_E_INVALID_KEYWORD,
    JSON_E_INVALID_ESCAPE_SEQUENCE,
    JSON_E_INVALID_UNICODE_SEQUENCE,
    JSON_E_INVALID_NUMBER,
    JSON_E_NESTING_DEPTH_REACHED,
    JSON_E_UNBALANCED_COLLECTION,
    JSON_E_EXPECTED_KEY,
    JSON_E_EXPECTED_COLON,
    JSON_E_OUT_OF_MEMORY
} JSON_error;

typedef enum 
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_INTEGER,
    JSON_T_FLOAT,
    JSON_T_NULL,
    JSON_T_TRUE,
    JSON_T_FALSE,
    JSON_T_STRING,
    JSON_T_KEY,
    JSON_T_MAX
} JSON_type;

typedef struct JSON_value_struct {
    union {
        JSON_int_t integer_value;
        
        double float_value;
        
        struct {
            const char* value;
            size_t length;
        } str;
    } vu;
} JSON_value;

typedef struct JSON_parser_struct* JSON_parser;

/*! \brief JSON parser callback 

    \param ctx The pointer passed to new_JSON_parser.
    \param type An element of JSON_type but not JSON_T_NONE.    
    \param value A representation of the parsed value. This parameter is NULL for
        JSON_T_ARRAY_BEGIN, JSON_T_ARRAY_END, JSON_T_OBJECT_BEGIN, JSON_T_OBJECT_END,
        JSON_T_NULL, JSON_T_TRUE, and JSON_T_FALSE. String values are always returned
        as zero-terminated C strings.

    \return Non-zero if parsing should continue, else zero.
*/    
typedef int (*JSON_parser_callback)(void* ctx, int type, const struct JSON_value_struct* value);


/**
   A typedef for allocator functions semantically compatible with malloc().
*/
typedef void* (*JSON_malloc_t)(size_t n);
/**
   A typedef for deallocator functions semantically compatible with free().
*/
typedef void (*JSON_free_t)(void* mem);

/*! \brief Create a JSON parser object 

    \param config. Used to configure the parser. Set to NULL to use
        the default configuration. See init_JSON_config.  Its contents are
        copied by this function, so it need not outlive the returned
        object.
    
    \return The parser object, which is owned by the caller and must eventually
    be freed by calling delete_JSON_parser().
*/
JSON_PARSER_DLL_API JSON_parser new_JSON_parser(JSON_parser_callback callback, void *ctx);

/*! \brief Destroy a previously created JSON parser object. */
JSON_PARSER_DLL_API void delete_JSON_parser(JSON_parser jc);

/*! \brief Parse a character.

    \return Non-zero, if all characters passed to this function are part of are valid JSON.
*/
JSON_PARSER_DLL_API int JSON_parser_char(JSON_parser jc, int next_char);

/*! \brief Finalize parsing.

    Call this method once after all input characters have been consumed.
    
    \return Non-zero, if all parsed characters are valid JSON, zero otherwise.
*/
JSON_PARSER_DLL_API int JSON_parser_done(JSON_parser jc);

/*! \brief Determine if a given string is valid JSON white space 

    \return Non-zero if the string is valid, zero otherwise.
*/
JSON_PARSER_DLL_API int JSON_parser_is_legal_white_space_string(const char* s);

/*! \brief Gets the last error that occurred during the use of JSON_parser.

    \return A value from the JSON_error enum.
*/
JSON_PARSER_DLL_API int JSON_parser_get_last_error(JSON_parser jc);

/*! \brief Re-sets the parser to prepare it for another parse run.

    \return True (non-zero) on success, 0 on error (e.g. !jc).
*/
JSON_PARSER_DLL_API int JSON_parser_reset(JSON_parser jc);

#ifdef _MSC_VER
#   if _MSC_VER >= 1400 /* Visual Studio 2005 and up */
#	  pragma warning(disable:4996) /* unsecure sscanf */
#	  pragma warning(disable:4127) /* conditional expression is constant */
#   endif
#endif

#define __   -1	 /* the universal error code */

/* values chosen so that the object size is approx equal to one page (4K) */
#ifndef JSON_PARSER_STACK_SIZE
#   define JSON_PARSER_STACK_SIZE 128
#endif

#ifndef JSON_PARSER_PARSE_BUFFER_SIZE
#   define JSON_PARSER_PARSE_BUFFER_SIZE 3500
#endif

typedef void* (*JSON_debug_malloc_t)(size_t bytes, const char* reason);

#ifdef JSON_PARSER_DEBUG_MALLOC
#   define JSON_parser_malloc(func, bytes, reason) ((JSON_debug_malloc_t)func)(bytes, reason)
#else
#   define JSON_parser_malloc(func, bytes, reason) func(bytes)
#endif

typedef unsigned short UTF16;

struct JSON_parser_struct {
	JSON_parser_callback callback;
	void* ctx;
	signed char state, before_comment_state, type, escaped, comment, allow_comments, handle_floats_manually, error;
	char decimal_point;
	UTF16 utf16_high_surrogate;
	int current_char;
	int depth;
	int top;
	int stack_capacity;
	signed char* stack;
	char* parse_buffer;
	size_t parse_buffer_capacity;
	size_t parse_buffer_count;
	signed char static_stack[JSON_PARSER_STACK_SIZE];
	char static_parse_buffer[JSON_PARSER_PARSE_BUFFER_SIZE];
	JSON_malloc_t malloc;
	JSON_free_t free;
};

struct json_context
{
	union json *data[JSON_DEPTH_MAX];
	size_t length;
	struct string key;
};

#define COUNTOF(x) (sizeof(x)/sizeof(x[0])) 

/*
	Characters are mapped into these character classes. This allows for
	a significant reduction in the size of the state transition table.
*/

enum classes {
	C_SPACE,  /* space */
	C_WHITE,  /* other whitespace */
	C_LCURB,  /* {  */
	C_RCURB,  /* } */
	C_LSQRB,  /* [ */
	C_RSQRB,  /* ] */
	C_COLON,  /* : */
	C_COMMA,  /* , */
	C_QUOTE,  /* " */
	C_BACKS,  /* \ */
	C_SLASH,  /* / */
	C_PLUS,   /* + */
	C_MINUS,  /* - */
	C_POINT,  /* . */
	C_ZERO ,  /* 0 */
	C_DIGIT,  /* 123456789 */
	C_LOW_A,  /* a */
	C_LOW_B,  /* b */
	C_LOW_C,  /* c */
	C_LOW_D,  /* d */
	C_LOW_E,  /* e */
	C_LOW_F,  /* f */
	C_LOW_L,  /* l */
	C_LOW_N,  /* n */
	C_LOW_R,  /* r */
	C_LOW_S,  /* s */
	C_LOW_T,  /* t */
	C_LOW_U,  /* u */
	C_ABCDF,  /* ABCDF */
	C_E,	  /* E */
	C_ETC,	/* everything else */
	C_STAR,   /* * */   
	NR_CLASSES
};

static const signed char ascii_class[128] = {
/*
	This array maps the 128 ASCII characters into character classes.
	The remaining Unicode characters should be mapped to C_ETC.
	Non-whitespace control characters are errors.
*/
	__,	  __,	  __,	  __,	  __,	  __,	  __,	  __,
	__,	  C_WHITE, C_WHITE, __,	  __,	  C_WHITE, __,	  __,
	__,	  __,	  __,	  __,	  __,	  __,	  __,	  __,
	__,	  __,	  __,	  __,	  __,	  __,	  __,	  __,

	C_SPACE, C_ETC,   C_QUOTE, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_STAR,   C_PLUS,  C_COMMA, C_MINUS, C_POINT, C_SLASH,
	C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
	C_DIGIT, C_DIGIT, C_COLON, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,

	C_ETC,   C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,	 C_ABCDF, C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_LSQRB, C_BACKS, C_RSQRB, C_ETC,   C_ETC,

	C_ETC,   C_LOW_A, C_LOW_B, C_LOW_C, C_LOW_D, C_LOW_E, C_LOW_F, C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_LOW_L, C_ETC,   C_LOW_N, C_ETC,
	C_ETC,   C_ETC,   C_LOW_R, C_LOW_S, C_LOW_T, C_LOW_U, C_ETC,   C_ETC,
	C_ETC,   C_ETC,   C_ETC,   C_LCURB, C_ETC,   C_RCURB, C_ETC,   C_ETC
};


/*
	The state codes.
*/
enum states {
	GO,  /* start	*/
	OK,  /* ok	   */
	OB,  /* object   */
	KE,  /* key	  */
	CO,  /* colon	*/
	VA,  /* value	*/
	AR,  /* array	*/
	ST,  /* string   */
	ES,  /* escape   */
	U1,  /* u1	   */
	U2,  /* u2	   */
	U3,  /* u3	   */
	U4,  /* u4	   */
	MI,  /* minus	*/
	ZE,  /* zero	 */
	IT,  /* integer  */
	FR,  /* fraction */
	E1,  /* e		*/
	E2,  /* ex	   */
	E3,  /* exp	  */
	T1,  /* tr	   */
	T2,  /* tru	  */
	T3,  /* true	 */
	F1,  /* fa	   */
	F2,  /* fal	  */
	F3,  /* fals	 */
	F4,  /* false	*/
	N1,  /* nu	   */
	N2,  /* nul	  */
	N3,  /* null	 */
	C1,  /* /		*/
	C2,  /* / *	 */
	C3,  /* *		*/
	FX,  /* *.* *eE* */
	D1,  /* second UTF-16 character decoding started by \ */
	D2,  /* second UTF-16 character proceeded by u */
	NR_STATES
};

enum actions
{
	CB = -10, /* comment begin */
	CE = -11, /* comment end */
	FA = -12, /* false */
	TR = -13, /* false */
	NU = -14, /* null */
	DE = -15, /* double detected by exponent e E */
	DF = -16, /* double detected by fraction . */
	SB = -17, /* string begin */
	MX = -18, /* integer detected by minus */
	ZX = -19, /* integer detected by zero */
	IX = -20, /* integer detected by 1-9 */
	EX = -21, /* next char is escaped */
	UC = -22  /* Unicode character read */
};

///*start  GO*/ {GO,GO,-6,__,-5,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
static const signed char state_transition_table[NR_STATES][NR_CLASSES] = {
/*
	The state transition table takes the current state and the current symbol,
	and returns either a new state or an action. An action is represented as a
	negative number. A JSON text is accepted if at the end of the text the
	state is OK and if the mode is MODE_DONE.

				 white									  1-9								   ABCDF  etc
			 space |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  |  a  b  c  d  e  f  l  n  r  s  t  u  |  E  |  * */
/*start  GO*/ {GO,GO,-6,__,-5,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ok	 OK*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*object OB*/ {OB,OB,__,-9,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*key	 KE*/ {KE,KE,__,__,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*colon  CO*/ {CO,CO,__,__,__,__,-2,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*value  VA*/ {VA,VA,-6,__,-5,__,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__},
/*array  AR*/ {AR,AR,-6,__,-5,-7,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__},
/*string ST*/ {ST,__,ST,ST,ST,ST,ST,ST,-4,EX,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST},
/*escape ES*/ {__,__,__,__,__,__,__,__,ST,ST,ST,__,__,__,__,__,__,ST,__,__,__,ST,__,ST,ST,__,ST,U1,__,__,__,__},
/*u1	 U1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U2,U2,U2,U2,U2,U2,U2,U2,__,__,__,__,__,__,U2,U2,__,__},
/*u2	 U2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U3,U3,U3,U3,U3,U3,U3,U3,__,__,__,__,__,__,U3,U3,__,__},
/*u3	 U3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U4,U4,U4,U4,U4,U4,U4,U4,__,__,__,__,__,__,U4,U4,__,__},
/*u4	 U4*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,UC,UC,UC,UC,UC,UC,UC,UC,__,__,__,__,__,__,UC,UC,__,__},
/*minus  MI*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,ZE,IT,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*zero   ZE*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*int	 IT*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,IT,IT,__,__,__,__,DE,__,__,__,__,__,__,__,__,DE,__,__},
/*frac   FR*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__},
/*e	     E1*/ {__,__,__,__,__,__,__,__,__,__,__,E2,E2,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ex	 E2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*exp	 E3*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*tr	 T1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T2,__,__,__,__,__,__,__},
/*tru	 T2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T3,__,__,__,__},
/*true   T3*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__},
/*fa	 F1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*fal	 F2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F3,__,__,__,__,__,__,__,__,__},
/*fals   F3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F4,__,__,__,__,__,__},
/*false  F4*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__},
/*nu	 N1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N2,__,__,__,__},
/*nul	 N2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N3,__,__,__,__,__,__,__,__,__},
/*null   N3*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__},
/*/	     C1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,C2},
/*/*	 C2*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/**	     C3*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,CE,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/*_.	 FX*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__},
/*\	     D1*/ {__,__,__,__,__,__,__,__,__,D2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*\	     D2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U1,__,__,__,__},
};


/*
	These modes can be pushed on the stack.
*/
enum modes {
	MODE_ARRAY = 1, 
	MODE_DONE = 2,  
	MODE_KEY = 3,   
	MODE_OBJECT = 4
};

static void set_error(JSON_parser jc)
{
	switch (jc->state) {
		case GO:
			switch (jc->current_char) {
			case '{': case '}': case '[': case ']': 
				jc->error = JSON_E_UNBALANCED_COLLECTION;
				break;
			default:
				jc->error = JSON_E_INVALID_CHAR;
				break;	
			}
			break;
		case OB:
			jc->error = JSON_E_EXPECTED_KEY;
			break;
		case AR:
			jc->error = JSON_E_UNBALANCED_COLLECTION;
			break;
		case CO:
			jc->error = JSON_E_EXPECTED_COLON;
			break;
		case KE:
			jc->error = JSON_E_EXPECTED_KEY;
			break;
		/* \uXXXX\uYYYY */
		case U1: case U2: case U3: case U4: case D1: case D2:
			jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
			break;
		/* true, false, null */
		case T1: case T2: case T3: case F1: case F2: case F3: case F4: case N1: case N2: case N3:
			jc->error = JSON_E_INVALID_KEYWORD;
			break;
		/* minus, integer, fraction, exponent */
		case MI: case ZE: case IT: case FR: case E1: case E2: case E3:
			jc->error = JSON_E_INVALID_NUMBER;
			break;
		default:
			jc->error = JSON_E_INVALID_CHAR;
			break;
	}
}

static int
push(JSON_parser jc, int mode)
{
/*
	Push a mode onto the stack. Return false if there is overflow.
*/
	assert(jc->top <= jc->stack_capacity);
	
	if (jc->depth < 0) {
		if (jc->top == jc->stack_capacity) {
			const size_t bytes_to_copy = jc->stack_capacity * sizeof(jc->stack[0]);
			const size_t new_capacity = jc->stack_capacity * 2;
			const size_t bytes_to_allocate = new_capacity * sizeof(jc->stack[0]);
			void* mem = JSON_parser_malloc(jc->malloc, bytes_to_allocate, "stack");
			if (!mem) {
				jc->error = JSON_E_OUT_OF_MEMORY;
				return false;
			}
			jc->stack_capacity = (int)new_capacity;
			memcpy(mem, jc->stack, bytes_to_copy);
			if (jc->stack != &jc->static_stack[0]) {
				jc->free(jc->stack);
			}
			jc->stack = (signed char*)mem;
		}
	} else {
		if (jc->top == jc->depth) {
			jc->error = JSON_E_NESTING_DEPTH_REACHED;
			return false;
		}
	}
	jc->stack[++jc->top] = (signed char)mode;
	return true;
}


static int
pop(JSON_parser jc, int mode)
{
/*
	Pop the stack, assuring that the current mode matches the expectation.
	Return false if there is underflow or if the modes mismatch.
*/
	if (jc->top < 0 || jc->stack[jc->top] != mode) {
		return false;
	}
	jc->top -= 1;
	return true;
}


#define parse_buffer_clear(jc) \
	do {\
		jc->parse_buffer_count = 0;\
		jc->parse_buffer[0] = 0;\
	} while (0)
	
#define parse_buffer_pop_back_char(jc)\
	do {\
		assert(jc->parse_buffer_count >= 1);\
		--jc->parse_buffer_count;\
		jc->parse_buffer[jc->parse_buffer_count] = 0;\
	} while (0)	



void delete_JSON_parser(JSON_parser jc)
{
	if (jc) {
		if (jc->stack != &jc->static_stack[0]) {
			jc->free((void*)jc->stack);
		}
		if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
			jc->free((void*)jc->parse_buffer);
		}
		jc->free((void*)jc);
	 }   
}

int JSON_parser_reset(JSON_parser jc)
{
	if (NULL == jc) {
		return false;
	}
	
	jc->state = GO;
	jc->top = -1;

	/* parser has been used previously? */
	if (NULL == jc->parse_buffer) {
	
		/* Do we want non-bound stack? */
		if (jc->depth > 0) {
			jc->stack_capacity = jc->depth;
			if (jc->depth <= (int)COUNTOF(jc->static_stack)) {
				jc->stack = &jc->static_stack[0];
			} else {
				const size_t bytes_to_alloc = jc->stack_capacity * sizeof(jc->stack[0]);
				jc->stack = (signed char*)JSON_parser_malloc(jc->malloc, bytes_to_alloc, "stack");
				if (jc->stack == NULL) {
					return false;
				}
			}
		} else {
			jc->stack_capacity = (int)COUNTOF(jc->static_stack);
			jc->depth = -1;
			jc->stack = &jc->static_stack[0];
		}
		
		/* set up the parse buffer */
		jc->parse_buffer = &jc->static_parse_buffer[0];
		jc->parse_buffer_capacity = COUNTOF(jc->static_parse_buffer);
	}
	
	/* set parser to start */
	push(jc, MODE_DONE);
	parse_buffer_clear(jc);
	
	return true;
}

JSON_parser new_JSON_parser(JSON_parser_callback callback, void *ctx)
{
/*
	new_JSON_parser starts the checking process by constructing a JSON_parser
	object. It takes a depth parameter that restricts the level of maximum
	nesting.

	To continue the process, call JSON_parser_char for each character in the
	JSON text, and then call JSON_parser_done to obtain the final result.
	These functions are fully reentrant.
*/

	JSON_parser jc;

	jc = JSON_parser_malloc(malloc, sizeof(*jc), "parser");	
	if (0 == jc) return 0;
	
	/* configure the parser */
	memset(jc, 0, sizeof(*jc));
	jc->malloc = malloc;
	jc->free = free;
	jc->callback = callback;
	jc->ctx = ctx;
	jc->allow_comments = true;
	jc->handle_floats_manually = false;
	jc->decimal_point = *localeconv()->decimal_point;
	jc->depth = JSON_DEPTH_MAX;
	
	/* reset the parser */
	if (!JSON_parser_reset(jc)) {
		jc->free(jc);
		return 0;
	}
	
	return jc;
}

static int parse_buffer_grow(JSON_parser jc)
{
	const size_t bytes_to_copy = jc->parse_buffer_count * sizeof(jc->parse_buffer[0]);
	const size_t new_capacity = jc->parse_buffer_capacity * 2;
	const size_t bytes_to_allocate = new_capacity * sizeof(jc->parse_buffer[0]);
	void* mem = JSON_parser_malloc(jc->malloc, bytes_to_allocate, "parse buffer");
	
	if (mem == NULL) {
		jc->error = JSON_E_OUT_OF_MEMORY;
		return false;
	}
	
	assert(new_capacity > 0);
	memcpy(mem, jc->parse_buffer, bytes_to_copy);
	
	if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
		jc->free(jc->parse_buffer);
	}
	
	jc->parse_buffer = (char*)mem;
	jc->parse_buffer_capacity = new_capacity;
	
	return true;
}

static int parse_buffer_reserve_for(JSON_parser jc, unsigned chars)
{
	while (jc->parse_buffer_count + chars + 1 > jc->parse_buffer_capacity) {
		if (!parse_buffer_grow(jc)) {
			assert(jc->error == JSON_E_OUT_OF_MEMORY);
			return false;
		}
	}
	
	return true;
}

#define parse_buffer_has_space_for(jc, count) \
	(jc->parse_buffer_count + (count) + 1 <= jc->parse_buffer_capacity)

#define parse_buffer_push_back_char(jc, c)\
	do {\
		assert(parse_buffer_has_space_for(jc, 1)); \
		jc->parse_buffer[jc->parse_buffer_count++] = c;\
		jc->parse_buffer[jc->parse_buffer_count]   = 0;\
	} while (0)

#define assert_is_non_container_type(jc) \
	assert( \
		jc->type == JSON_T_NULL || \
		jc->type == JSON_T_FALSE || \
		jc->type == JSON_T_TRUE || \
		jc->type == JSON_T_FLOAT || \
		jc->type == JSON_T_INTEGER || \
		jc->type == JSON_T_STRING)
	

static int parse_parse_buffer(JSON_parser jc)
{
	if (jc->callback) {
		JSON_value value, *arg = NULL;
		
		if (jc->type != JSON_T_NONE) {
			assert_is_non_container_type(jc);
		
			switch(jc->type) {
				case JSON_T_FLOAT:
					arg = &value;
					if (jc->handle_floats_manually) {
						value.vu.str.value = jc->parse_buffer;
						value.vu.str.length = jc->parse_buffer_count;
					} else { 
						/* not checking with end pointer b/c there may be trailing ws */
						value.vu.float_value = strtod(jc->parse_buffer, NULL);
					}
					break;
				case JSON_T_INTEGER:
					arg = &value;
					sscanf(jc->parse_buffer, JSON_PARSER_INTEGER_SSCANF_TOKEN, &value.vu.integer_value);
					break;
				case JSON_T_STRING:
					arg = &value;
					value.vu.str.value = jc->parse_buffer;
					value.vu.str.length = jc->parse_buffer_count;
					break;
			}
			
			if (!(*jc->callback)(jc->ctx, jc->type, arg)) {
				return false;
			}
		}
	}
	
	parse_buffer_clear(jc);
	
	return true;
}

#define IS_HIGH_SURROGATE(uc) (((uc) & 0xFC00) == 0xD800)
#define IS_LOW_SURROGATE(uc)  (((uc) & 0xFC00) == 0xDC00)
#define DECODE_SURROGATE_PAIR(hi,lo) ((((hi) & 0x3FF) << 10) + ((lo) & 0x3FF) + 0x10000)
static const unsigned char utf8_lead_bits[4] = { 0x00, 0xC0, 0xE0, 0xF0 };

static int decode_unicode_char(JSON_parser jc)
{
	int i;
	unsigned uc = 0;
	char* p;
	int trail_bytes;
	
	assert(jc->parse_buffer_count >= 6);
	
	p = &jc->parse_buffer[jc->parse_buffer_count - 4];
	
	for (i = 12; i >= 0; i -= 4, ++p) {
		unsigned x = *p;
		
		if (x >= 'a') {
			x -= ('a' - 10);
		} else if (x >= 'A') {
			x -= ('A' - 10);
		} else {
			x &= ~0x30u;
		}
		
		assert(x < 16);
		
		uc |= x << i;
	}
	
	/* clear UTF-16 char from buffer */
	jc->parse_buffer_count -= 6;
	jc->parse_buffer[jc->parse_buffer_count] = 0;
	
	/* attempt decoding ... */
	if (jc->utf16_high_surrogate) {
		if (IS_LOW_SURROGATE(uc)) {
			uc = DECODE_SURROGATE_PAIR(jc->utf16_high_surrogate, uc);
			trail_bytes = 3;
			jc->utf16_high_surrogate = 0;
		} else {
			/* high surrogate without a following low surrogate */
			return false;
		}
	} else {
		if (uc < 0x80) {
			trail_bytes = 0;
		} else if (uc < 0x800) {
			trail_bytes = 1;
		} else if (IS_HIGH_SURROGATE(uc)) {
			/* save the high surrogate and wait for the low surrogate */
			jc->utf16_high_surrogate = (UTF16)uc;
			return true;
		} else if (IS_LOW_SURROGATE(uc)) {
			/* low surrogate without a preceding high surrogate */
			return false;
		} else {
			trail_bytes = 2;
		}
	}
	
	jc->parse_buffer[jc->parse_buffer_count++] = (char) ((uc >> (trail_bytes * 6)) | utf8_lead_bits[trail_bytes]);
	
	for (i = trail_bytes * 6 - 6; i >= 0; i -= 6) {
		jc->parse_buffer[jc->parse_buffer_count++] = (char) (((uc >> i) & 0x3F) | 0x80);
	}

	jc->parse_buffer[jc->parse_buffer_count] = 0;
	
	return true;
}

static int add_escaped_char_to_parse_buffer(JSON_parser jc, int next_char)
{
	assert(parse_buffer_has_space_for(jc, 1));
	
	jc->escaped = 0;
	/* remove the backslash */
	parse_buffer_pop_back_char(jc);
	switch(next_char) {
		case 'b':
			parse_buffer_push_back_char(jc, '\b');
			break;
		case 'f':
			parse_buffer_push_back_char(jc, '\f');
			break;
		case 'n':
			parse_buffer_push_back_char(jc, '\n');
			break;
		case 'r':
			parse_buffer_push_back_char(jc, '\r');
			break;
		case 't':
			parse_buffer_push_back_char(jc, '\t');
			break;
		case '"':
			parse_buffer_push_back_char(jc, '"');
			break;
		case '\\':
			parse_buffer_push_back_char(jc, '\\');
			break;
		case '/':
			parse_buffer_push_back_char(jc, '/');
			break;
		case 'u':
			parse_buffer_push_back_char(jc, '\\');
			parse_buffer_push_back_char(jc, 'u');
			break;
		default:
			return false;
	}

	return true;
}

static int add_char_to_parse_buffer(JSON_parser jc, int next_char, int next_class)
{
	if (!parse_buffer_reserve_for(jc, 1)) {
		assert(JSON_E_OUT_OF_MEMORY == jc->error);
		return false;
	}
	
	if (jc->escaped) {
		if (!add_escaped_char_to_parse_buffer(jc, next_char)) {
			jc->error = JSON_E_INVALID_ESCAPE_SEQUENCE;
			return false; 
		}
	} else if (!jc->comment) {
		if ((jc->type != JSON_T_NONE) | !((next_class == C_SPACE) | (next_class == C_WHITE)) /* non-white-space */) {
			parse_buffer_push_back_char(jc, (char)next_char);
		}
	}
	
	return true;
}

#define assert_type_isnt_string_null_or_bool(jc) \
	assert(jc->type != JSON_T_FALSE); \
	assert(jc->type != JSON_T_TRUE); \
	assert(jc->type != JSON_T_NULL); \
	assert(jc->type != JSON_T_STRING)


int
JSON_parser_char(JSON_parser jc, int next_char)
{
/*
	After calling new_JSON_parser, call this function for each character (or
	partial character) in your JSON text. It can accept UTF-8, UTF-16, or
	UTF-32. It returns true if things are looking ok so far. If it rejects the
	text, it returns false.
*/
	int next_class, next_state;

/*
	Store the current char for error handling
*/	
	jc->current_char = next_char;
	
/*
	Determine the character's class.
*/
	if (next_char < 0) {
		jc->error = JSON_E_INVALID_CHAR;
		return false;
	}
	if (next_char >= 128) {
		next_class = C_ETC;
	} else {
		next_class = ascii_class[next_char];
		if (next_class <= __) {
			set_error(jc);
			return false;
		}
	}
	
	if (!add_char_to_parse_buffer(jc, next_char, next_class)) {
		return false;
	}
	
/*
	Get the next state from the state transition table.
*/
	next_state = state_transition_table[jc->state][next_class];
	if (next_state >= 0) {
/*
	Change the state.
*/
		jc->state = (signed char)next_state;
	} else {
/*
	Or perform one of the actions.
*/
		switch (next_state) {
/* Unicode character */		
		case UC:
			if(!decode_unicode_char(jc)) {
				jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
				return false;
			}
			/* check if we need to read a second UTF-16 char */
			if (jc->utf16_high_surrogate) {
				jc->state = D1;
			} else {
				jc->state = ST;
			}
			break;
/* escaped char */
		case EX:
			jc->escaped = 1;
			jc->state = ES;
			break;
/* integer detected by minus */
		case MX:
			jc->type = JSON_T_INTEGER;
			jc->state = MI;
			break;  
/* integer detected by zero */			
		case ZX:
			jc->type = JSON_T_INTEGER;
			jc->state = ZE;
			break;  
/* integer detected by 1-9 */			
		case IX:
			jc->type = JSON_T_INTEGER;
			jc->state = IT;
			break;  
			
/* floating point number detected by exponent*/
		case DE:
			assert_type_isnt_string_null_or_bool(jc);
			jc->type = JSON_T_FLOAT;
			jc->state = E1;
			break;   
		
/* floating point number detected by fraction */
		case DF:
			assert_type_isnt_string_null_or_bool(jc);
			if (!jc->handle_floats_manually) {
/*
	Some versions of strtod (which underlies sscanf) don't support converting 
	C-locale formated floating point values.
*/		   
				assert(jc->parse_buffer[jc->parse_buffer_count-1] == '.');
				jc->parse_buffer[jc->parse_buffer_count-1] = jc->decimal_point;
			}			
			jc->type = JSON_T_FLOAT;
			jc->state = FX;
			break;   
/* string begin " */
		case SB:
			parse_buffer_clear(jc);
			assert(jc->type == JSON_T_NONE);
			jc->type = JSON_T_STRING;
			jc->state = ST;
			break;		
		
/* n */
		case NU:
			assert(jc->type == JSON_T_NONE);
			jc->type = JSON_T_NULL;
			jc->state = N1;
			break;		
/* f */
		case FA:
			assert(jc->type == JSON_T_NONE);
			jc->type = JSON_T_FALSE;
			jc->state = F1;
			break;		
/* t */
		case TR:
			assert(jc->type == JSON_T_NONE);
			jc->type = JSON_T_TRUE;
			jc->state = T1;
			break;		
		
/* closing comment */
		case CE:
			jc->comment = 0;
			assert(jc->parse_buffer_count == 0);
			assert(jc->type == JSON_T_NONE);
			jc->state = jc->before_comment_state;
			break;		
		
/* opening comment  */
		case CB:
			if (!jc->allow_comments) {
				return false;
			}
			parse_buffer_pop_back_char(jc);
			if (!parse_parse_buffer(jc)) {
				return false;
			}
			assert(jc->parse_buffer_count == 0);
			assert(jc->type != JSON_T_STRING);
			switch (jc->stack[jc->top]) {
			case MODE_ARRAY:
			case MODE_OBJECT:   
				switch(jc->state) {
				case VA:
				case AR:
					jc->before_comment_state = jc->state;
					break;
				default:
					jc->before_comment_state = OK;
					break;
				}
				break;
			default:
				jc->before_comment_state = jc->state;
				break;
			}
			jc->type = JSON_T_NONE;
			jc->state = C1;
			jc->comment = 1;
			break;
/* empty } */
		case -9:		
			parse_buffer_clear(jc);
			if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, NULL)) {
				return false;
			}
			if (!pop(jc, MODE_KEY)) {
				return false;
			}
			jc->state = OK;
			break;

/* } */ case -8:
			parse_buffer_pop_back_char(jc);
			if (!parse_parse_buffer(jc)) {
				return false;
			}
			if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, NULL)) {
				return false;
			}
			if (!pop(jc, MODE_OBJECT)) {
				jc->error = JSON_E_UNBALANCED_COLLECTION;
				return false;
			}
			jc->type = JSON_T_NONE;
			jc->state = OK;
			break;

/* ] */ case -7:
			parse_buffer_pop_back_char(jc);
			if (!parse_parse_buffer(jc)) {
				return false;
			}
			if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_ARRAY_END, NULL)) {
				return false;
			}
			if (!pop(jc, MODE_ARRAY)) {
				jc->error = JSON_E_UNBALANCED_COLLECTION;
				return false;
			}
			
			jc->type = JSON_T_NONE;
			jc->state = OK;
			break;

/* { */ case -6:
			parse_buffer_pop_back_char(jc);
			if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_BEGIN, NULL)) {
				return false;
			}
			if (!push(jc, MODE_KEY)) {
				return false;
			}
			assert(jc->type == JSON_T_NONE);
			jc->state = OB;
			break;

/* [ */ case -5:
			parse_buffer_pop_back_char(jc);
			if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_ARRAY_BEGIN, NULL)) {
				return false;
			}
			if (!push(jc, MODE_ARRAY)) {
				return false;
			}
			assert(jc->type == JSON_T_NONE);
			jc->state = AR;
			break;

/* string end " */ case -4:
			parse_buffer_pop_back_char(jc);
			switch (jc->stack[jc->top]) {
			case MODE_KEY:
				assert(jc->type == JSON_T_STRING);
				jc->type = JSON_T_NONE;
				jc->state = CO;
				
				if (jc->callback) {
					JSON_value value;
					value.vu.str.value = jc->parse_buffer;
					value.vu.str.length = jc->parse_buffer_count;
					if (!(*jc->callback)(jc->ctx, JSON_T_KEY, &value)) {
						return false;
					}
				}
				parse_buffer_clear(jc);
				break;
			case MODE_ARRAY:
			case MODE_OBJECT:
			case MODE_DONE: // martinkunev
				assert(jc->type == JSON_T_STRING);
				if (!parse_parse_buffer(jc)) {
					return false;
				}
				jc->type = JSON_T_NONE;
				jc->state = OK;
				break;
			default:
				return false;
			}
			break;

/* , */ case -3:
			parse_buffer_pop_back_char(jc);
			if (!parse_parse_buffer(jc)) {
				return false;
			}
			switch (jc->stack[jc->top]) {
			case MODE_OBJECT:
/*
	A comma causes a flip from object mode to key mode.
*/
				if (!pop(jc, MODE_OBJECT) || !push(jc, MODE_KEY)) {
					return false;
				}
				assert(jc->type != JSON_T_STRING);
				jc->type = JSON_T_NONE;
				jc->state = KE;
				break;
			case MODE_ARRAY:
				assert(jc->type != JSON_T_STRING);
				jc->type = JSON_T_NONE;
				jc->state = VA;
				break;
			default:
				return false;
			}
			break;

/* : */ case -2:
/*
	A colon causes a flip from key mode to object mode.
*/
			parse_buffer_pop_back_char(jc);
			if (!pop(jc, MODE_KEY) || !push(jc, MODE_OBJECT)) {
				return false;
			}
			assert(jc->type == JSON_T_NONE);
			jc->state = VA;
			break;
/*
	Bad action.
*/
		default:
			set_error(jc);
			return false;
		}
	}
	return true;
}

int
JSON_parser_done(JSON_parser jc)
{
	if ((jc->state == OK || jc->state == GO) && pop(jc, MODE_DONE))
	{
		return true;
	}

	jc->error = JSON_E_UNBALANCED_COLLECTION;
	return false;
}


int JSON_parser_is_legal_white_space_string(const char* s)
{
	int c, char_class;
	
	if (s == NULL) {
		return false;
	}
	
	for (; *s; ++s) {   
		c = *s;
		
		if (c < 0 || c >= 128) {
			return false;
		}
		
		char_class = ascii_class[c];
		
		if (char_class != C_SPACE && char_class != C_WHITE) {
			return false;
		}
	}
	
	return true;
}

int JSON_parser_get_last_error(JSON_parser jc)
{
	return jc->error;
}

// Returns the position of the Most Significant Bit set in a byte
static inline unsigned char msb_pos(unsigned char byte)
{
	static const unsigned char mask[] = {0x1, 0x3, 0xf};
	unsigned char r, s;
	// Round 0
	r = ((mask[2] & byte) < byte) << 2; // No need to use s here as r is initially zero
	byte >>= r;
	// Round 1
	s = ((mask[1] & byte) < byte) << 1;
	byte >>= s;
	r += s;
	// Round 2
	return r + ((mask[0] & byte) < byte); // Just return the position
}

// TODO: memory errors can cause memory leaks

static int token_add(void *restrict context, int type, const JSON_value *value)
{
	struct json_context *stack = (struct json_context *)context;
	union json *item = malloc(sizeof(union json)), *parent;
	if (!item) return 0; // Memory error

	// Get node parent
	if (stack->length) parent = stack->data[stack->length - 1];
	else parent = 0;

	switch (type)
	{
		union json **node;
	case JSON_T_NULL:
		json_type(item) = NONE;
		break;
	case JSON_T_TRUE:
		json_type(item) = BOOLEAN;
		item->boolean = true;
		break;
	case JSON_T_FALSE:
		json_type(item) = BOOLEAN;
		item->boolean = false;
		break;
	case JSON_T_INTEGER:
		json_type(item) = INTEGER;
		item->integer = value->vu.integer_value;
		break;
	case JSON_T_FLOAT:
		json_type(item) = REAL;
		item->real = value->vu.float_value;
		break;
	case JSON_T_STRING:
		free(item);
		{
			struct string entry = string((char *)value->vu.str.value, value->vu.str.length); // TODO fix this cast
			item = json_string_old(&entry);
		}
		if (!item) goto error; // memory error
		break;
	case JSON_T_ARRAY_BEGIN:
		json_type(item) = ARRAY;
		if (!vector_init(&item->array_node, VECTOR_SIZE_BASE)) goto error; // memory error
		node = stack->data + stack->length;
		*node = item;
		stack->length += 1;
		break;
	case JSON_T_OBJECT_BEGIN:
		json_type(item) = OBJECT;
		item->object = malloc(sizeof(struct dict));
		if (!item->object) goto error; // memory error
		if (!dict_init(item->object, DICT_SIZE_BASE))
		{
			free(item->object);
			goto error; // memory error
		}
		node = stack->data + stack->length;
		*node = item;
		stack->length += 1;
		break;
	case JSON_T_ARRAY_END:
	case JSON_T_OBJECT_END:
		free(item);
		stack->length -= 1;
		return 1;
	case JSON_T_KEY:
		free(item);
		if (!string_init(&stack->key, value->vu.str.value, value->vu.str.length)) return 0; // memory error
		return 1;
	}

	if (parent)
	{
		// Add the node to its parent node
		switch (json_type(parent))
		{
			bool error;
		case ARRAY:
			if (!vector_add(&parent->array_node, item)) goto error;
			break;
		case OBJECT:
			error = dict_add(parent->object, &stack->key, item);
			free(stack->key.data);
			if (error) goto error;
			break;
		}
	}
	else *stack->data = item;

	return 1;

error:
	// Memory error
	if (json_type(parent) == OBJECT) free(stack->key.data);
	free(item);
	return 0;
}

void json_free(union json *restrict json)
{
	if (!json) return; // mimic the behavior of free()
	switch (json_type(json))
	{
	case OBJECT:
		// TODO: this can be implemented better if dict_term() supports custom free function; fix dict_term() and merge these two functions
		{
			struct dict_iterator it;
			struct dict_item *prev;
			struct dict *dict = json->object;

			// Free each item in each slot of the dictionary
			for(it.index = 0; it.index < dict->size; ++it.index)
				if (dict->items[it.index])
				{
					it.item = dict->items[it.index];
					do
					{
						prev = it.item;
						it.item = it.item->_next;

						json_free(prev->value);
						free(prev);
					} while (it.item);
				}

			free(dict->items);
		}
		break;
	case ARRAY:
		{
			size_t length = json->array_node.length;
			while (length--) json_free(json->array_node.data[length]);
			vector_term(&json->array_node);
		}
		break;
	}
	free(json);
}

// TODO: decide how to distinguish parse error from internal server error
union json *json_parse(const struct string *json)
{
	struct json_context context = {.length = 0};
	struct JSON_parser_struct *jc = new_JSON_parser(&token_add, &context); // TODO: memory error
	size_t i;

	for(i = 0; i < json->length; ++i)
		if (!JSON_parser_char(jc, (unsigned char)json->data[i])) // TODO: memory or parse error
			goto error;

	if (!JSON_parser_done(jc)) goto error; // TODO: parse error

	delete_JSON_parser(jc);

	return *context.data;

error:
	if (*context.data) json_free(*context.data);
	delete_JSON_parser(jc);
	return 0;
}

static size_t utf8_read(uint32_t *restrict result, const unsigned char *start, size_t length)
{
	size_t i = 0;

	unsigned char msb = msb_pos(~*start);
	size_t bytes = 7 - msb;
	if (!bytes) bytes = 1;
	if (bytes > length) return 0;

	if (result)
	{
		uint32_t character = (start[i++] & ((1 << msb) - 1)) << ((bytes - 1) * 6);
		while (--bytes)
			character |= (start[i++] & 0x3f) << ((bytes - 1) * 6);

		*result = character;
		return i;
	}
	else return bytes;
}

ssize_t json_length_string(const char *restrict data, size_t size)
{
	unsigned char *start = (unsigned char *)data;
	size_t length = 0;
	size_t i = 0;
	while (i < size)
	{
		if ((0x1f < start[i]) && (start[i] < 0x7f)) // If this is a valid character in ASCII
		{
			if ((start[i] == '"') || (start[i] == '\\'))
				length += 2;
			else
				length += 1;
		}
		else if ((start[i] == '\t') || (start[i] == '\n')) length += 2;
		else if (start[i] <= 0x1f) length += 6;
		else
		{
			// Advance the position properly for multi-byte characters
			size_t bytes = utf8_read(0, start + i, size - i);
			if (!bytes) return -1;
			length += bytes;
			i += bytes;
			continue;
		}
		++i;
	}
	return length;
}

ssize_t json_length(const union json *restrict json)
{
	static const size_t boolean_length[] = {5, 4};

	switch (json_type(json))
	{
		size_t count;
		size_t i;
		ssize_t size;
	case NONE:
		return 4;
	case OBJECT:
		{
			struct dict_iterator it;
			const struct dict_item *item;
			count = 2 + json->object->count - (json->object->count > 0); // Non-null objects start with { and end with } and separate elements with ,
			for(item = dict_first(&it, json->object); item; item = dict_next(&it, json->object))
			{
				size = json_length_string(item->key_data, item->key_size);
				if (size < 0) return -1;
				count += 1 + size + 1 + 1; // "data":
				size = json_length(item->value);
				if (size < 0) return -1;
				count += size;
			}
		}
		return count;
	case ARRAY:
		i = json->array_node.length;
		count = 2 + i - (json->array_node.length > 0); // Arrays start with [ and end with ] and separate elements with ,
		while (i--) // We are just counting now so we can walk through the elements in reverse order
		{
			size = json_length(json->array_node.data[i]);
			if (size < 0) return -1;
			count += size;
		}
		return count;
	case BOOLEAN:
		return boolean_length[json->boolean];
	case INTEGER:
		return format_int_length(json->integer, 10);
	case REAL:
		return snprintf(0, 0, "%f", json->real);
	case STRING:
		size = json_length_string(json->string_node.data, json->string_node.length);
		if (size < 0) return -1;
		return 1 + size + 1; // "data"
	}
}

char *json_dump_string(unsigned char *restrict dest, const unsigned char *restrict src, size_t size)
{
	size_t i = 0;
	while (i < size)
	{
		// Check for \t and \n to optimize size as they are commonly encountered.
		if ((0x1f < src[i]) && (src[i] < 0x7f)) // if this is a valid character in ASCII
		{
			if ((src[i] == '"') || (src[i] == '\\'))
				*dest++ = '\\';
			*dest++ = src[i];
		}
		else if (src[i] == '\t')
		{
			*dest++ = '\\';
			*dest++ = 't';
		}
		else if (src[i] == '\n')
		{
			*dest++ = '\\';
			*dest++ = 'n';
		}
		else if (src[i] <= 0x1f)
		{
			uint32_t code;
			uint16_t character;
			i += utf8_read(&code, src + i, size - i);
			character = htobe16(code);

			// Write the encoded character.
			*dest++ = '\\';
			*dest++ = 'u';
			dest = format_hex(dest, (char *)&character, sizeof(character));

			continue;
		}
		else
		{
			size_t bytes = utf8_read(0, src + i, size - i);
			while (bytes--) *dest++ = src[i++];
			continue;
		}
		++i;
	}
	return dest;
}

char *json_dump(char *restrict result, const union json *restrict json)
{
	static const char *boolean_data[] = {"false", "true"};

	switch (json_type(json))
	{
		size_t length;
		size_t i;
	case NONE:
		result[0] = 'n';
		result[1] = 'u';
		result[2] = 'l';
		result[3] = 'l';
		return result + 4;
	case OBJECT:
		{
			struct dict_iterator it;
			const struct dict_item *item;
			bool first = true;
			*result++ = '{';
			for(item = dict_first(&it, json->object); item; item = dict_next(&it, json->object))
			{
				if (first) first = false;
				else *result++ = ','; // , as a separator
				*result++ = '"'; // " before key string
				result = json_dump_string(result, item->key_data, item->key_size);
				*result++ = '"'; // " after key string
				*result++ = ':'; // : after the key
				result = json_dump(result, item->value);
			}
			*result++ = '}';
		}
		return result;
	case ARRAY:
		length = json->array_node.length;
		*result++ = '[';
		for(i = 0; i < length; ++i)
		{
			if (i) *result++ = ','; // , as a separator
			result = json_dump(result, json->array_node.data[i]);
		}
		*result++ = ']';
		return result;
	case BOOLEAN:
		return result + sprintf(result, "%s", boolean_data[json->boolean]);
	case INTEGER:
		return format_int(result, json->integer, 10);
	case REAL:
		return result + sprintf(result, "%f", json->real);
	case STRING:
		*result++ = '"'; // " before key string
		result = json_dump_string(result, json->string_node.data, json->string_node.length);
		*result++ = '"'; // " after key string
		return result;
	}
}

struct string *json_serialize(const union json *json)
{
	ssize_t length = json_length(json);
	if (length < 0) return 0;

	// Allocate one continuous memory region for the string and its data so that it can be freed with free()
	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0; // Memory error
	result->length = length;
	result->data = (char *)(result + 1);

	json_dump(result->data, json);
	result->data[result->length] = 0;
	return result;
}

union json *json_none(void)
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // memory error
	json_type(result) = NONE;
	return result;
}

union json *json_boolean(bool value)
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // memory error
	json_type(result) = BOOLEAN;
	result->boolean = value;
	return result;
}

union json *json_integer(long long value)
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // memory error
	json_type(result) = INTEGER;
	result->integer = value;
	return result;
}

union json *json_real(double value)
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // memory error
	json_type(result) = REAL;
	result->real = value;
	return result;
}

union json *json_string(const char *data, size_t length)
{
	union json *result = malloc(sizeof(union json) + length + 1);
	if (!result) return 0; // memory error
	json_type(result) = STRING;
	result->string_node.data = (char *)(result + 1);
	result->string_node.length = length;
	*format_bytes(result->string_node.data, data, length) = 0;
	return result;
}

union json *json_string_old(const struct string *value)
{
	union json *result = malloc(sizeof(union json) + sizeof(char) * (value->length + 1));
	if (!result) return 0; // memory error
	json_type(result) = STRING;
	result->string_node = string((char *)(result + 1), value->length);
	memcpy(result->string_node.data, value->data, value->length);
	result->string_node.data[value->length] = 0;
	return result;
}

union json *json_array(void)
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // memory error
	json_type(result) = ARRAY;
	if (!vector_init(&result->array_node, VECTOR_SIZE_BASE))
	{
		free(result);
		return 0; // memory error
	}
	return result;
}

union json *json_object(void)
{
	union json *result = malloc(sizeof(union json) + sizeof(struct dict));
	if (!result) return 0; // memory error
	json_type(result) = OBJECT;
	result->object = (struct dict *)(result + 1);
	if (!dict_init(result->object, DICT_SIZE_BASE))
	{
		free(result);
		return 0; // memory error
	}
	return result;
}

union json *json_array_insert(union json *restrict container, union json *restrict value)
{
	if (container && value && vector_add(&container->array_node, value)) return container;
	else
	{
		json_free(container);
		json_free(value);
		return 0;
	}
}

union json *json_object_insert(union json *restrict container, const struct string *restrict key, union json *restrict value)
{
	if (container && value && !dict_add(container->object, key, value)) return container;
	else
	{
		json_free(container);
		json_free(value);
		return 0;
	}
}

union json *json_object_old(bool is_null) // WARNING: Deprecated; this function leaks memory
{
	union json *result = malloc(sizeof(union json));
	if (!result) return 0; // Memory error
	if (is_null) json_type(result) = NONE;
	else
	{
		json_type(result) = OBJECT;
		result->object = malloc(sizeof(struct dict));
		if (!result->object) return 0; // memory error
		if (!dict_init(result->object, DICT_SIZE_BASE))
		{
			free(result->object);
			return 0; // memory error
		}
	}
	return result;
}
int json_array_insert_old(union json *restrict parent, union json *restrict child)
{
	if (!child) return -1; // child not initialized (usually a memory error)
	int error = !vector_add(&parent->array_node, child);
	if (error) json_free(child);
	return (error ? -1 : 0); // TODO fix error codes
}
int json_object_insert_old(union json *restrict parent, const struct string *key, union json *restrict value)
{
	if (!value) return -1; // value not initialized (usually a memory error)
	int error = dict_add(parent->object, key, value);
	if (error) json_free(value);
	return error;
}

union json *json_clone(const union json *json)
{
	if (!json) return 0;

	switch (json_type(json))
	{
	case NONE:
		return json_none();
	case BOOLEAN:
		return json_boolean(json->boolean);
	case INTEGER:
		return json_integer(json->integer);
	case REAL:
		return json_real(json->real);
	case STRING:
		return json_string(json->string_node.data, json->string_node.length);
	case ARRAY:
		{
			union json *result, *value;

			size_t index, length;

			result = json_array();

			length = json->array_node.length;
			for(index = 0; index < length; ++index)
			{
				value = json_clone(vector_get(&json->array_node, index));
				result = json_array_insert(result, value);
			}

			return result;
		}
	case OBJECT:
		{
			union json *result, *value;

			struct dict_iterator it;
			const struct dict_item *item;
			struct string key;

			result = json_object();

			for(item = dict_first(&it, json->object); item; item = dict_next(&it, json->object))
			{
				key = string((char *)item->key_data, item->key_size);
				value = json_clone(item->value);
				result = json_object_insert(result, &key, value);
			}

			return result;
		}
	}
}

//  http://www.ietf.org/rfc/rfc4627.txt
//  2.5.  Strings

// WARNING: For testing
/*int main(int argc, char *argv[])
{
	struct string s;
	s.data = argv[1];
	s.length = strlen(argv[1]);

	union json *root, *temp;

	//root = json_object_old(false);
	root = json_parse(&s);
	
	if (root)
	{
		struct string *r = json_serialize(root);

		printf("%s\n", r->data);

		free(r);
		json_free(root);
	}
	else printf("Syntax error\n");

	return 0;
}*/
