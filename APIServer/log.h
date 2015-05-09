#define Debug 1
#define Warning 2
#define Error 3
#define Fail 4

#ifndef RUN_MODE
# define RUN_MODE Warning
#endif

#if !defined(DEBUG) && (RUN_MODE == Debug)
# define DEBUG
#endif

// TODO remove RUN_MODE

// TODO trace documentation
// The highest 2 bits of the length store next argument's type.

#define _logs(s, l, ...)	((0x3L << (sizeof(size_t) * 8 - 2)) | (l)), (s)
#define logs(...)			_logs(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)
#define logi(i)				(0x2L << (sizeof(size_t) * 8 - 2)), ((int64_t)(i))

void trace(int fd, ...);
#define trace(...) (trace)(__VA_ARGS__, (size_t)0)

// Use these to find logging location.
#if 0
# define _line_str_ex(l) #l
# define _line_str(l) _line_str_ex(l)
# define LOCATION logs(__FILE__ ":" _line_str(__LINE__) " "),
#else
#define LOCATION
#endif

#if defined(DEBUG)
# define debug(...) trace(2, LOCATION __VA_ARGS__)
#else
# define debug(...)
#endif
#define warning(...) trace(2, LOCATION __VA_ARGS__)
#define error(...) trace(2, LOCATION logs("Error: "), __VA_ARGS__)

// TODO The code below is deprecated

#define log_(level, message) do \
	{ \
		if ((level) >= RUN_MODE) write(2, (message), sizeof(message) - 1); \
	} while (false)

// Debug message.
#define debug_(message) log_(Debug, (message "\n"))

// Minor problem.
#define warning_(message) log_(Warning, (message "\n"))

// Major problem.
#define error_(message) log_(Error, (message "\n"))

// Critical problem. Terminate application.
#ifdef OS_BSD
# define fail(code, message) do \
	{ \
		log_(Fail, (message "\n")); \
		_exit(code); \
	} while (false)
#else
# define fail(code, message) do \
	{ \
		log_(Fail, (message "\n")); \
		exit(code); \
	} while (false)
#endif

// TODO: what about access logs ?
