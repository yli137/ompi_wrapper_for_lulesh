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

void *handler(void *arg)
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

	int iter = 0;
	while(poll(&pollfd, 1, -1) > 0){
		nread = read(fargs->uffd, &msg, sizeof(msg));

		if (nread == 0 || nread == -1) {
			continue;
		}

		// Handle the write fault
		if (msg.event == UFFD_EVENT_PAGEFAULT) {
			unsigned long print_fault_address = msg.arg.pagefault.address;
#if 0
			printf("thread %d thread# %d addr %ld caught a fault %lld WRITE %d WP %d MINOR %d WRITE+WP %d\n",
					getpid(), fargs->rank, print_fault_address,
					msg.arg.pagefault.flags,
					UFFD_PAGEFAULT_FLAG_WRITE,
					UFFD_PAGEFAULT_FLAG_WP,
					UFFD_PAGEFAULT_FLAG_MINOR,
					UFFD_PAGEFAULT_FLAG_WRITE | UFFD_PAGEFAULT_FLAG_WP);
#endif
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


				for(int i = 0; i < pair_size; i++){
					if(fault_address >= (unsigned long)pair[i].isend_addr &&
							fault_address < (unsigned long)pair[i].isend_addr + pair[i].isend_size){

						if(pthread_mutex_trylock(&(pair[i].pair_lock)) == 0){
						int comp_ret = compress_lz4_buffer(pair[i].isend_addr, 
								pair[i].isend_size,
								pair[i].comp_addr, 
								pair[i].comp_size);

						pair[i].comp_size = comp_ret != 0 ? comp_ret : pair[i].comp_size;

							pthread_mutex_unlock(&(pair[i].pair_lock));
						}
					}
				}

				usleep(10);
				uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
				if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
					perror("UFFDIO_WRITEPROTECT");
					exit(EXIT_FAILURE);
				}

			}
		}
	}

	return NULL;
}


void uffd_register(char *addr, size_t size, int rank, int first){
	int page_size = sysconf(_SC_PAGE_SIZE);
	char *region = (char*)((unsigned long)addr & ~(page_size - 1));
	size_t region_size = (size + page_size - 1) / page_size * page_size;

	if(add_reg_pair(region, region_size)){
		char *region = reg_list->list[reg_list->pos-1].region;
		size_t region_size = reg_list->list[reg_list->pos-1].size;

		// Step 1: Create a userfaultfd object
		//int uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
		int uffd = fargs->uffd;
		assert(uffd != -1);

#if 0
		// Step 2: Register the memory with userfaultfd
		struct uffdio_api uffdio_api;
		uffdio_api.api = UFFD_API;
		uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
		assert(ioctl(uffd, UFFDIO_API, &uffdio_api) != -1);
#endif

		// Step 3: set up address and flags
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (unsigned long)region;
		uffdio_register.range.len = region_size;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP; 
		uffdio_register.ioctls = 0;
		assert(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) != -1);

		struct uffdio_writeprotect uffdio_wp;
		uffdio_wp.range.start = (unsigned long)region;
		uffdio_wp.range.len = region_size;
		uffdio_wp.mode = 0;
		assert(ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) != -1);

		uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
		assert(ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) != -1);

#if 0
		// Step 4: Spawn a thread to handle page faults
		pthread_t uffd_thread;

		struct fault_handler_args *args = (struct fault_handler_args*)malloc(sizeof(struct fault_handler_args));
		args->uffd = uffd;
		args->length = region_size;
		args->address = (void*)region;
		args->rank = rank;

		add_fault_args(uffd, region_size, (void*)region, rank);

		printf("*************created a thread***************\n");
		assert(pthread_create(&uffd_thread, NULL, handler, (void*)args) == 0);
#endif
	}
}



