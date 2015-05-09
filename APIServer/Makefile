export CC=gcc
export CFLAGS=-std=c99 -pthread -O2 -DDEBUG -D_BSD_SOURCE -D_POSIX_SOURCE -D_DEFAULT_SOURCE -Werror -Wno-parentheses -Wno-empty-body -Wno-return-type -Wno-switch -Wchar-subscripts -Wimplicit -Wsequence-point -Wno-pointer-sign
export LDFLAGS=-std=c99 -pthread -O2

SRC=main.o http_response.o http_parse.o http.o json.o stream.o log.o dictionary.o vector.o format.o storage.o actions/article.o actions/example.o

all: $(SRC)
	$(CC) $(LDFLAGS) $^ -o server

clean:
	rm -f $(SRC)
	rm -f server
