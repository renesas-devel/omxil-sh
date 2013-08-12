/**
    src/vpu5/memallochelper.c

    Copyright (c) 2013  Igel Co., Ltd.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this file (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to permit
    persons to whom the Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

/*  This simple program is a way to hold a device file open for an indefinite
    length of time. UIO devices using the dmem_uio_genirq driver will allocate
    memory resources on the first device open and free them on the last device
    exit.  If the device is expected to be opened and closed multiple times
    within a short interval, this can lead to a lot of unnecessary freeing and
    allocating of DMA memory regions.  This program provides a simple way to add
    another user to a specific UIO device to force allocation or keep an allocated
    DMA region from being freed. */

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>


void usage(char *name) {
	printf("Usage: %s <path to device file>\n", name);
}

int main(int argc, char *argv[]) {
	int fd;
	int pipe_fds[2];

	struct pollfd pollev;

	if (argc != 2) {
		usage(argv[0]);
		return -1;
	}

	if (strncmp("/dev/uio", argv[1], 8)) {
		usage(argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDONLY);

	if (pipe(pipe_fds) == -1) {
		perror("Open pipes");
		return -1;
	}


	/* Block on the read pipe indefinitely */
	pollev.fd = pipe_fds[0];
	pollev.events = POLLIN;	

	poll(&pollev, 1, -1);

	close(pipe_fds[0]);
	close(pipe_fds[1]);

	return 0;	
}	
