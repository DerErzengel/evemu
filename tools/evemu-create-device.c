/*****************************************************************************
 * evemu-create-device - Standalone utility to create and hold a virtual input device
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "evemu.h"

// Opens the device node and prints its path to the console
static int open_evemu_device(struct evemu_device *dev)
{
    int fd;
    const char *device_node = evemu_get_devnode(dev);

    if (!device_node) {
        fprintf(stderr, "error: cannot determine device node\n");
        return -1;
    }

    fd = open(device_node, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "error %d opening %s: %s\n", errno, device_node, strerror(errno));
        return -1;
    }
    
    // Output the device path so the user/scripts know where it was created
    fprintf(stdout, "Created device: '%s' at %s\n", evemu_get_name(dev), device_node);
    fflush(stdout);

    return fd;
}

// Blocks execution by continuously reading from the device.
// This keeps the process alive and prevents the kernel from destroying the virtual device.
static void open_and_hold_device(struct evemu_device *dev)
{
    char data[256];
    int ret;
    int fd;

    fd = open_evemu_device(dev);
    if (fd < 0)
        return;

    // Block here indefinitely. It also consumes events to prevent kernel buffer overflows.
    while ((ret = read(fd, data, sizeof(data))) > 0)
        ;

    close(fd);
}

int main(int argc, char *argv[])
{
    FILE *fp;
    struct evemu_device *dev;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device.prop>\n", argv[0]);
        return EXIT_FAILURE;
    }

    fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "error: could not open file %s (%m)\n", argv[1]);
        return EXIT_FAILURE;
    }

    dev = evemu_new(NULL);
    if (!dev) {
        fprintf(stderr, "error: could not allocate evemu device\n");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (evemu_read(dev, fp) <= 0) {
        fprintf(stderr, "error: failed to read device properties from %s\n", argv[1]);
        evemu_delete(dev);
        fclose(fp);
        return EXIT_FAILURE;
    }

    // Ensure the device has a name before creating it
    if (strlen(evemu_get_name(dev)) == 0) {
        char name[64];
        snprintf(name, sizeof(name), "evemu-%d", getpid());
        evemu_set_name(dev, name);
    }

    // Create the virtual input device in the kernel
    if (evemu_create_managed(dev) < 0) {
        fprintf(stderr, "error: could not create virtual device\n");
        evemu_delete(dev);
        fclose(fp);
        return EXIT_FAILURE;
    }

    // Hang the program here to keep the device alive until the user presses Ctrl+C
    open_and_hold_device(dev);

    // Cleanup (usually only reached if the read loop fails)
    evemu_delete(dev);
    fclose(fp);

    return EXIT_SUCCESS;
}