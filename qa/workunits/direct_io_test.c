/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
#define _GNU_SOURCE // for O_DIRECT

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * direct_io_test
 *
 * This test does some I/O using O_DIRECT.
 *
 * Semantics of O_DIRECT can be found at http://lwn.net/Articles/348739/
 *
 */

struct chunk {
	uint64_t offset;
	uint64_t pad0;
	uint64_t pad1;
	uint64_t pad2;
	uint64_t pad3;
	uint64_t pad4;
	uint64_t pad5;
	uint64_t not_offset;
} __attribute__((packed));

static int page_size;

static char temp_file[] = "direct_io_temp_file_XXXXXX";

static int safe_write(int fd, const void *buf, signed int len)
{
	const char *b = (const char*)buf;
	/* Handle EINTR and short writes */
	while (1) {
		int res = write(fd, b, len);
		if (res < 0) {
			int err = errno;
			if (err != EINTR) {
				return err;
			}
		}
		len -= res;
		b += res;
		if (len <= 0)
			return 0;
	}
}

static int do_read(int fd, char *buf, int buf_sz)
{
	/* We assume no short reads or EINTR. It's not really clear how
	 * those things interact with O_DIRECT. */
	int ret = read(fd, buf, buf_sz);
	if (ret < 0) {
		int err = errno;
		printf("do_read: error: %d (%s)\n", err, strerror(err));
		return err;
	}
	if (ret != buf_sz) {
		printf("do_read: short read\n");
		return -EIO;
	}
	return 0;
}

static int setup_temp_file(void)
{
	int fd;
	int64_t num_chunks, i;

	if (page_size % sizeof(struct chunk) != 0) {
		printf("setup_big_file: page_size doesn't divide evenly "
			"into data blocks.\n");
		return -EINVAL;
	}

	fd = mkostemps(temp_file, 0, O_WRONLY | O_TRUNC);
	if (fd < 0) {
		int err = errno;
		printf("setup_big_file: mkostemps failed with error %d\n", err);
		return err;
	}

	num_chunks = page_size / sizeof(struct chunk);
	for (i = 0; i < num_chunks; ++i) {
		int ret;
		struct chunk c;
		memset(&c, 0, sizeof(c));
		c.offset = i * sizeof(struct chunk);
		c.pad0 = 0;
		c.pad1 = 1;
		c.pad2 = 2;
		c.pad3 = 3;
		c.pad4 = 4;
		c.pad5 = 5;
		c.not_offset = ~c.offset;
		ret = safe_write(fd, &c, sizeof(struct chunk));
		if (ret) {
			printf("setup_big_file: safe_write failed with "
			       "error: %d\n", ret);
			TEMP_FAILURE_RETRY(close(fd));
			unlink(temp_file);
			return ret;
		}
	}
	TEMP_FAILURE_RETRY(close(fd));
	return 0;
}

static int verify_chunk(const struct chunk *c, uint64_t offset)
{
	if (c->offset != offset) {
		printf("verify_chunk(%" PRId64 "): bad offset value\n", offset);
		return EIO;
	}
	if (c->pad0 != 0) {
		printf("verify_chunk(%" PRId64 "): bad pad0 value\n", offset);
		return EIO;
	}
	if (c->pad1 != 1) {
		printf("verify_chunk(%" PRId64 "): bad pad1 value\n", offset);
		return EIO;
	}
	if (c->pad2 != 2) {
		printf("verify_chunk(%" PRId64 "): bad pad2 value\n", offset);
		return EIO;
	}
	if (c->pad3 != 3) {
		printf("verify_chunk(%" PRId64 "): bad pad3 value\n", offset);
		return EIO;
	}
	if (c->pad4 != 4) {
		printf("verify_chunk(%" PRId64 "): bad pad4 value\n", offset);
		return EIO;
	}
	if (c->pad5 != 5) {
		printf("verify_chunk(%" PRId64 "): bad pad5 value\n", offset);
		return EIO;
	}
	if (c->not_offset != ~offset) {
		printf("verify_chunk(%" PRId64 "): bad pad5 value\n", offset);
		return EIO;
	}
	return 0;
}

static int do_o_direct_reads(void)
{
	int fd, ret;
	void *buf = 0;
	ret = posix_memalign(&buf, page_size, page_size);
	if (ret) {
		printf("do_o_direct_reads: posix_memalign returned %d\n", ret);
		goto done;
	}

	fd = open(temp_file, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		ret = errno;
		printf("do_o_direct_reads: error opening fd: %d\n", ret);
		goto free_buf;
	}

	// read the first chunk and see if it looks OK
	ret = do_read(fd, buf, page_size);
	if (ret)
		goto close_fd;

	ret = verify_chunk((struct chunk*)buf, 0);
	if (ret)
		goto close_fd;

//	n = lseek(fd, 0, SEEK_END);
//	printf("seek %d\n", n);
//	n = lseek(fd, 0, SEEK_CUR);
//	printf("seek %d\n", n);
//	n = lseek(fd, 6656, SEEK_SET);
//	printf("seek %d\n", n);
//
//	n = posix_memalign(&buf, 512, 8192);
//	printf("posix_memalign ret %d buf %p\n", n, buf);
//	memset(buf, 0, n);
//
//	n = read(fd, buf, 8192);
//	printf("read %d bytes (err %d)\n", n, errno);
//	char* buf1 = (char*)buf;
//	for (i = 0; i < n; ++i)
//		printf("%c", buf1[i]);
//	printf("\n");

close_fd:
	TEMP_FAILURE_RETRY(close(fd));
free_buf:
	free(buf);
done:
	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	page_size = getpagesize();

	ret = setup_temp_file();
	if (ret) {
		printf("setup_temp_file failed with error %d\n", ret);
		goto done;
	}

	ret = do_o_direct_reads();
	if (ret) {
		printf("do_o_direct_reads failed with error %d\n", ret);
		goto unlink_temp_file;
	}

unlink_temp_file:
	unlink(temp_file);
done:
	return ret;
}
