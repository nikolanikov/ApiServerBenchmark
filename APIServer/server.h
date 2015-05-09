#include <arpa/inet.h>

struct resources
{
	struct stream stream;
	struct sockaddr_storage address;
	void *storage;
};
