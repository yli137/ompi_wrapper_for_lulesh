#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "wrapper.h"

static void *handler(void *arg)
{
	struct fault_handler_args* fargs = (struct fault_handler_args*) arg;
	struct uffd_msg msg;
	size_t region_size = fargs->length;
	ssize_t nread;

	struct pollfd pollfd;
	pollfd.fd = fargs->uffd;
	pollfd.events = POLLIN;

	int page_size = sysconf(_SC_PAGE_SIZE);
	// Allocate and map a new page
	void *new_page = mmap(NULL, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (new_page == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	while (poll(&pollfd, 1, -1) > 0) {
		printf("got a poll\n");
		nread = read(fargs->uffd, &msg, sizeof(msg));
		
		if (nread == 0 || nread == -1) {
			perror("Error reading userfaultfd event");
			continue;
		}

		// Handle the write fault
		if (msg.event == UFFD_EVENT_PAGEFAULT) {
			printf("caught a fault %lld WRITE %d WP %d MINOR %d WRITE+WP %d\n",
					msg.arg.pagefault.flags,
					UFFD_PAGEFAULT_FLAG_WRITE,
					UFFD_PAGEFAULT_FLAG_WP,
					UFFD_PAGEFAULT_FLAG_MINOR,
					UFFD_PAGEFAULT_FLAG_WRITE | UFFD_PAGEFAULT_FLAG_WP);
			if (msg.arg.pagefault.flags == UFFD_PAGEFAULT_FLAG_WP) {
				unsigned long fault_address = msg.arg.pagefault.address;

				struct uffdio_writeprotect uffdio_wp;
				//uffdio_wp.range.start = fault_address & ~(page_size - 1);
				uffdio_wp.range.start = fault_address;
				uffdio_wp.range.len = page_size;
				uffdio_wp.mode = 0;

				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT");
					exit(EXIT_FAILURE);
				}

				uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT (reapply)");
					exit(EXIT_FAILURE);
				}

			} else if (msg.arg.pagefault.flags == (UFFD_PAGEFAULT_FLAG_WP | UFFD_PAGEFAULT_FLAG_WRITE)){
				unsigned long fault_address = msg.arg.pagefault.address;

				struct uffdio_writeprotect uffdio_wp;
				//uffdio_wp.range.start = fault_address & ~(page_size - 1);
				uffdio_wp.range.start = fault_address;
				uffdio_wp.range.len = page_size;
				uffdio_wp.mode = 0;

				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT");
					exit(EXIT_FAILURE);
				}
				
				usleep(1);
				uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT");
					exit(EXIT_FAILURE);
				}

			} else if (msg.arg.pagefault.flags == UFFD_PAGEFAULT_FLAG_WRITE){
				unsigned long fault_address = msg.arg.pagefault.address;

				void *new_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (new_page == MAP_FAILED) {
					perror("mmap");
					exit(EXIT_FAILURE);
				}

				memset(new_page, 0, page_size);

				struct uffdio_copy uffdio_copy;
				uffdio_copy.src = (unsigned long)new_page;
				//uffdio_copy.dst = fault_address & ~(page_size - 1);  // Align to page boundary
				uffdio_copy.dst = fault_address;  // Align to page boundary
				uffdio_copy.len = page_size;
				uffdio_copy.mode = 0;

				if (ioctl(fargs->uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
					perror("UFFDIO_COPY");
					exit(EXIT_FAILURE);
				}

				munmap(new_page, page_size);


				struct uffdio_writeprotect uffdio_wp;
				//uffdio_wp.range.start = fault_address & ~(page_size - 1);
				uffdio_wp.range.start = fault_address;
				uffdio_wp.range.len = page_size;
				uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT");
					exit(EXIT_FAILURE);
				}


			} else {
				unsigned long fault_address = msg.arg.pagefault.address;

				memset(new_page, 0, page_size);  // Zero-fill the page

				struct uffdio_copy uffdio_copy;
				uffdio_copy.src = (unsigned long)new_page;
				//uffdio_copy.dst = fault_address & ~(page_size - 1);  // Align to page boundary
				uffdio_copy.dst = fault_address;  // Align to page boundary
				uffdio_copy.len = page_size;
				uffdio_copy.mode = 0;
				uffdio_copy.copy = 0;

				if (ioctl(fargs->uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
					perror("UFFDIO_COPY");
					exit(EXIT_FAILURE);
				}

				struct uffdio_writeprotect uffdio_wp;
				uffdio_wp.range.start = fault_address & ~(page_size - 1);
				uffdio_wp.range.len = page_size;
				uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT (reapply)");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	return NULL;
}


void uffd_register(char *addr, size_t size, int rank){
	int page_size = sysconf(_SC_PAGE_SIZE);
	printf("addr %p region %p page_size %d input_size %lu\n", 
			addr, (char*)((unsigned long)addr & ~(page_size - 1)),
			page_size,
			size);
	char *region = (char*)((unsigned long)addr & ~(page_size - 1));
	size_t region_size = (size + page_size - 1) / page_size * page_size;

	add_reg_pair(region, region_size);

	// Step 1: Create a userfaultfd object
	int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	assert(uffd != -1);

	// Step 2: Register the memory with userfaultfd
	struct uffdio_api uffdio_api;
	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
	assert(ioctl(uffd, UFFDIO_API, &uffdio_api) != -1);

	// Step 3: set up address and flags
	struct uffdio_register uffdio_register;
	uffdio_register.range.start = (unsigned long)region;
	uffdio_register.range.len = region_size;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP; 

	int ioret = ioctl(uffd, UFFDIO_REGISTER, &uffdio_register);
	if(ioret == -1){
		printf("rank %d error\n", rank);
		perror("ioctl error\n");
	}

	// Step 4: Spawn a thread to handle page faults
	pthread_t uffd_thread;
	struct fault_handler_args *args = (struct fault_handler_args*)malloc(sizeof(struct fault_handler_args));
	args->uffd = uffd;
	args->length = region_size;
	args->address = (void*)region;

	assert(pthread_create(&uffd_thread, NULL, handler, args) == 0);
	printf("created a thread\n");

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(rank + 16, &cpuset);

}



