/*****************************************************************************
 *
 * evemu - Kernel device emulation
 *
 * Copyright (C) 2010-2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 ****************************************************************************/

#define _GNU_SOURCE
#include "evemu.h"
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>   // ✅ FIXES MCL_CURRENT ERROR
#include <errno.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

// global in play.c
char *evemu_current_filename = NULL;

static int open_evemu_device(struct evemu_device *dev)
{
	int fd;
	const char *device_node = evemu_get_devnode(dev);

	if (!device_node) {
		fprintf(stderr, "can not determine device node\n");
		return -1;
	}

	fd = open(device_node, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "error %d opening %s: %s\n",
			errno, device_node, strerror(errno));
		return -1;
	}
	fprintf(stdout, "%s: %s\n", evemu_get_name(dev), device_node);
	fflush(stdout);

	return fd;
}

static void open_and_hold_device(struct evemu_device *dev)
{
	char data[256];
	int ret;
	int fd;

	fd = open_evemu_device(dev);
	if (fd < 0)
		return;

	while ((ret = read(fd, data, sizeof(data))) > 0)
		;

	close(fd);
}

static struct evemu_device* create_device(FILE *fp)
{
	struct evemu_device *dev;
	int ret = -ENOMEM;
	int saved_errno;

	dev = evemu_new(NULL);
	if (!dev)
		goto out;
	ret = evemu_read(dev, fp);
	if (ret <= 0)
		goto out;

	if (strlen(evemu_get_name(dev)) == 0) {
		char name[64];
		sprintf(name, "evemu-%d", getpid());
		evemu_set_name(dev, name);
	}

	ret = evemu_create_managed(dev);
	if (ret < 0)
		goto out;

out:
	if (ret && dev) {
		saved_errno = errno;
		evemu_destroy(dev);
		dev = NULL;
		errno = saved_errno;
	}
	return dev;
}

static int evemu_device(FILE *fp)
{
	struct evemu_device *dev;

	dev = create_device(fp);
	if (dev == NULL)
		return -1;

	open_and_hold_device(dev);
	evemu_delete(dev);

	return 0;
}


static int device(int argc, char *argv[])
{
	FILE *fp;
	int ret;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <dev.prop>\n", argv[0]);
		return -1;
	}
	fp = fopen(argv[1], "r");
	if (!fp) {
		fprintf(stderr, "error: could not open file (%m)\n");
		return -1;
	}
	ret = evemu_device(fp);
	if (ret <= 0) {
		fprintf(stderr, "error: could not create device: %d\n", ret);
		return -1;
	}
	fclose(fp);
	return 0;
}

static int play_from_stdin(int fd, long start_offset_us)
{
    int ret;
    printf("Starting replay from STDIN with offset %ld microseconds\n",
           start_offset_us);
    fflush(stdout);

    ret = evemu_play(stdin, fd, start_offset_us);

    if (ret != 0)
        fprintf(stderr, "error: could not replay device\n");

    return ret;
}

static int play_from_file(int device_fd, int recording_fd, long start_offset_us)
{
	FILE *fp;
	int ret;

	fp = fdopen(recording_fd, "r");
	if (!fp) {
		fprintf(stderr, "error: could not open file (%m)\n");
		return -1;
	}

	printf("Starting replay from file '%s' with offset %ld microseconds\n", evemu_current_filename ? evemu_current_filename : "unknown", start_offset_us);
	fflush(stdout);

	fseek(fp, 0, SEEK_SET);
	ret = evemu_play(fp, device_fd, start_offset_us);
	if (ret != 0) {
		fprintf(stderr, "error: could not replay device\n");
	}

	fclose(fp);
	return ret;
}

static int play(int argc, char *argv[])
{
	int device_fd;
	int recording_fd;
	struct stat st;
	long start_offset_us = 0;

	if (argc < 2 || argc > 4) {
		fprintf(stderr, "Usage: %s <device> [<recording>] [<offset_us>]\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "If only <device> is provided, event data is read from standard input.\n");
		fprintf(stderr, "If <recording> is provided, event data is read from the file.\n");
		return -1;
	}

	device_fd = open(argv[1], O_RDWR);
	if (device_fd < 0) {
		fprintf(stderr, "error: could not open device %s (%m)\n", argv[1]);
		return -1;
	}

	if (fstat(device_fd, &st) == -1) {
		fprintf(stderr, "error: failed to stat device %s (%m)\n", argv[1]);
		close(device_fd);
		return -1;
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "error: %s is not a character device\n", argv[1]);
		close(device_fd);
		return -1;
	}

	if (argc == 2) {
		play_from_stdin(device_fd, 0);
	} else {
		if (argc == 4) {
			start_offset_us = atol(argv[3]);
		}
		
		recording_fd = open(argv[2], O_RDONLY);
		if (recording_fd < 0) {
			fprintf(stderr, "error: could not open recording file %s (%m)\n", argv[2]);
			close(device_fd);
			return -1;
		}
		
		evemu_current_filename = argv[2];
		play_from_file(device_fd, recording_fd, start_offset_us);
	}

	close(device_fd);
	return 0;
}

int main(int argc, char *argv[])
{
    const char *prgm_name = program_invocation_short_name;

    // Otherwise, behave as normal
    if (prgm_name &&
        (strcmp(prgm_name, "evemu-device") == 0 ||
         strcmp(prgm_name, "lt-evemu-device") == 0))
        return device(argc, argv);
    else
        return play(argc, argv);
}
