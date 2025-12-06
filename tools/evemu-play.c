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

static int play_from_stdin(int fd)
{
	int ret;
	long start_offset_us = 0;
	ret = evemu_play(stdin, fd, start_offset_us);

	if (ret != 0)
		fprintf(stderr, "error: could not replay device\n");

	return ret;
}



static int play_from_file(int recording_fd,  long start_offset_us)
{
	FILE *fp;
	struct evemu_device *dev = NULL;
	int fd;

	fp = fdopen(recording_fd, "r");
	if (!fp) {
		fprintf(stderr, "error: could not open file (%m)\n");
		return -1;
	}

	dev = create_device(fp);
	if (!dev) {
		fprintf(stderr, "error: could not create device: %m\n");
		fclose(fp);
		return -1;
	}

	fd = open_evemu_device(dev);
	if (fd < 0)
		goto out;

	int ret;
	char line[32];

	
	printf("\n");
	printf("Starting Evemu Replay TEST.. FOR ME NEWE");
	fflush(stdout);

	fseek(fp, 0, SEEK_SET);
	ret = evemu_play(fp, fd, start_offset_us);
	if (ret != 0) {
		fprintf(stderr, "error: could not replay device\n");
	}

out:
	evemu_delete(dev);
	fclose(fp);
	close(fd);
	return 0;
}

static int play(int argc, char *argv[])
{
	int fd;
		struct stat st;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s <device>|<recording>\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "If the argument is an input event node,\n"
				"event data is read from standard input.\n");
		fprintf(stderr, "If the argument is an evemu recording,\n"
				"the device is created and the event data is"
				"read from the same device.\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "error: could not open file or device (%m)\n");
		return -1;
	}

	if (fstat(fd, &st) == -1) {
		fprintf(stderr, "error: failed to look at file (%m)\n");
		return -1;
	}

	long start_offset_us = 0;

	if (argc == 3) {
        start_offset_us = atol(argv[2]); // parse microsecond offset
		printf("StartOffset Replay: %ld", start_offset_us);
		fflush(stdout);
    }
	
	if (S_ISCHR(st.st_mode))
		play_from_stdin(fd);
	else
		play_from_file(fd, start_offset_us);


	close(fd);
	return 0;
}

void setup_realtime_cpu(void)
{
    cpu_set_t set;
    struct sched_param sp;

    /* -----------------------------
       1. LOCK TO A SINGLE CPU CORE
       ----------------------------- */

    CPU_ZERO(&set);
    CPU_SET(3, &set);   // ✅ CHANGE THIS CORE NUMBER

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity failed");
    } else {
        printf("✅ CPU pinned to core 3\n");
    }

    /* -----------------------------
       2. ENABLE REAL-TIME SCHEDULER
       ----------------------------- */

    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 99;  // ✅ 1–99 (higher = more priority)

    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("sched_setscheduler failed (are you root?)");
    } else {
        printf("✅ Real-time scheduler enabled (SCHED_FIFO)\n");
    }

    /* -----------------------------
       3. PREVENT SWAPPING (IMPORTANT)
       ----------------------------- */

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
    } else {
        printf("✅ Memory locked (no paging)\n");
    }
}

int main(int argc, char *argv[])
{
	setup_realtime_cpu();   // ✅ MUST BE FIRST
	const char *prgm_name = program_invocation_short_name;

	if (prgm_name &&
	    (strcmp(prgm_name, "evemu-device") == 0 ||
	     /* when run directly from the sources (not installed) */
	     strcmp(prgm_name, "lt-evemu-device") == 0))
		return device(argc, argv);
	else
		return play(argc, argv);
}
