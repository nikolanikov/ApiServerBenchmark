#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

#include "base.h"
#include "format.h"
#include "stream.h"
#include "storage.h"

#define WEBROOT "/tmp/data/"
#define FILENAME "Latest_plane_crash"

#define PATH_SIZE_LIMIT 4096

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static struct file_info *content = 0;

static void release(struct file_info *file_info)
{
	file_info->links -= 1;
	if (!file_info->links)
	{
		munmap(file_info->buffer, file_info->size);
		free(file_info);
	}
}

static int storage_load(const unsigned char *filename, unsigned version)
{
	struct stat info;
	int file;

	if (content) release(content);

	content = malloc(sizeof(*content));
	if (!content) return -1;

	file = open(filename, O_RDONLY);
	if (file < 0) return -2;
	if (fstat(file, &info) < 0)
	{
		close(file);
		return -3;
	}

	content->buffer = mmap(0, info.st_size, PROT_READ, MAP_PRIVATE, file, 0);
	//content->buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (content->buffer == MAP_FAILED) return -4;

	content->size = info.st_size;

	content->version = version;
	content->links = 1;

	return 0;
}

static void generate_path(unsigned char *restrict result, const unsigned char *restrict filename, size_t filename_size, unsigned version)
{
	char *position;
	position = format_bytes(result, WEBROOT, sizeof(WEBROOT) - 1);
	position = format_bytes(position, filename, filename_size);
	*position++ = '/';
	position = format_uint(position, version, 10);
	*position++ = 0;
}

static int latest_version(unsigned char *restrict path, size_t *restrict path_size, const unsigned char *restrict filename, size_t filename_size)
{
	unsigned char *position;
	position = format_bytes(path, WEBROOT, sizeof(WEBROOT) - 1);
	position = format_bytes(position, filename, filename_size);
	*position = 0;
	*path_size = position - path;

	struct dirent *entry, *more;

	DIR *dir = opendir(path);
	if (!dir) return -1; // TODO no such file
	entry = malloc(offsetof(struct dirent, d_name) + pathconf(path, _PC_NAME_MAX) + 1);
	if (!entry)
	{
		closedir(dir);
		return -1; // TODO memory error
	}

	int version = -1; // TODO no such file

	while (1)
	{
		char *end;
		long number;

		if (readdir_r(dir, entry, &more))
		{
			closedir(dir);
			return -1; // TODO readdir error
		}
		if (!more) break; // no more entries

		number = strtol(entry->d_name, &end, 10);
		if (*end) continue;

		if ((number < 0) || (number > INT_MAX)) continue;

		if (number > version) version = number;
	}

	closedir(dir);

	return version;
}

struct file_info *storage_get(const struct string *name)
{
	struct file_info *result = 0;

	pthread_mutex_lock(&mutex);

	if (!content)
	{
		char path[PATH_SIZE_LIMIT], *position;
		size_t path_size;
		int version = latest_version(path, &path_size, FILENAME, sizeof(FILENAME) - 1);
		if (version < 0) ; // TODO error check

		path[path_size++] = '/';
		position = format_uint(path + path_size, version, 10);
		*position++ = 0;

		if (storage_load(path, version))
			goto finally;
	}

	content->links += 1;
	result = content;

finally:
	pthread_mutex_unlock(&mutex);

	return result;
}

static int writeall(int fd, const char *buffer, size_t total)
{
    size_t index;
    ssize_t size;
    for(index = 0; index < total; index += size)
    {
        size = write(fd, buffer + index, total - index);
        if (size < 0) return -1;
    }
    return 0;
}

static int transfer(struct stream *input, int output, size_t size)
{
	struct string buffer;
	int status;
	while (size)
	{
		if (status = stream_read(input, &buffer, (size > BUFFER_SIZE_MAX) ? BUFFER_SIZE_MAX : size))
			return status;
		if (status = writeall(output, buffer.data, buffer.length))
			return status;
		stream_read_flush(input, buffer.length);
		size -= buffer.length;
	}
	return 0;
}

int storage_set(const struct string *restrict name, struct stream *restrict stream, size_t size)
{
	unsigned version;

	pthread_mutex_lock(&mutex);
	if (!content)
	{
		char path[PATH_SIZE_LIMIT], *position;
		size_t path_size;
		int version = latest_version(path, &path_size, FILENAME, sizeof(FILENAME) - 1);
		if (version < 0) ; // TODO error check

		path[path_size++] = '/';
		position = format_uint(path + path_size, version, 10);
		*position++ = 0;

		if (storage_load(path, version) < 0)
		{
			pthread_mutex_unlock(&mutex);
			return -1;
		}
	}
	version = content->version;
	pthread_mutex_unlock(&mutex);

	version += 1;

	// Generate full path for the new version of the file.
	char path[PATH_SIZE_LIMIT];
	generate_path(path, FILENAME, sizeof(FILENAME) - 1, version);

	int file;
	unsigned char *buffer;

	// Create the new file and write the data to it.
	file = creat(path, 0644);
	ftruncate(file, size);
	//buffer = mmap(0, size, PROT_WRITE, MAP_SHARED, file, 0);
	//close(file);
	/*if (buffer == MAP_FAILED)
	{
		int e = errno;
		unlink(path);
		return -2;
	}*/
	if (transfer(stream, file, size) < 0)
	{
		//munmap(buffer, size);
		close(file);
		unlink(path);
		return -3;
	}
	close(file);
	//munmap(buffer, size);

	pthread_mutex_lock(&mutex);
	storage_load(path, version);
	pthread_mutex_unlock(&mutex);

	return 0;
}

void storage_release(struct file_info *file_info)
{
	pthread_mutex_lock(&mutex);
	release(file_info);
	pthread_mutex_unlock(&mutex);
}
