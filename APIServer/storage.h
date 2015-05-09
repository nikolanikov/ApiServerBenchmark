struct file_info
{
	unsigned char *buffer;
	size_t size;
	unsigned version;

	unsigned links; // reference counting
};

struct file_info *storage_get(const struct string *name);
int storage_set(const struct string *restrict name, struct stream *restrict stream, size_t size);
void storage_release(struct file_info *file_info);
