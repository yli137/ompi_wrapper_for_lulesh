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
	ssize_t nread;

	struct pollfd pollfd;
	pollfd.fd = fargs->uffd;
	pollfd.events = POLLIN;

	int page_size = sysconf(_SC_PAGE_SIZE);

	while(poll(&pollfd, 1, -1) > 0){
		nread = read(fargs->uffd, &msg, sizeof(msg));
		if (nread == 0 || nread == -1) 
			continue;

		printf("got one\n");
		// Handle the write fault
		if (msg.event == UFFD_EVENT_PAGEFAULT) {
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

			} else if (msg.arg.pagefault.flags == (UFFD_PAGEFAULT_FLAG_WP | UFFD_PAGEFAULT_FLAG_WRITE)){

				unsigned long fault_address = msg.arg.pagefault.address;

				struct uffdio_writeprotect uffdio_wp;

				for(int i = 0; i < pair_size; i++){
					if(fault_address >= (unsigned long)(pair[i].isend_addr) &&
							fault_address < (unsigned long)(pair[i].isend_addr) + pair[i].isend_size){
						pair[i].ready = 0;
						if(pthread_mutex_trylock(&(pair[i].pair_lock)) == 0){
							pthread_mutex_unlock(&(pair[i].pair_lock));
						}
					}
				}

				for(int i = 0; i < reg_list->pos; i++){
					if(fault_address >= (unsigned long)(reg_list->list[i].region) && 
								fault_address < (unsigned long)(reg_list->list[i].region) + reg_list->list[i].size){
						uffdio_wp.range.start = (unsigned long)(reg_list->list[i].region);
						uffdio_wp.range.len = reg_list->list[i].size;
						uffdio_wp.mode = 0;
						
						if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
							perror("UFFDIO_WRITEPROTECT2");
							exit(EXIT_FAILURE);
						}
					}
				}
				usleep(1);
				
				for(int i = 0; i < reg_list->pos; i++){
					if(fault_address >= (unsigned long)(reg_list->list[i].region) && 
								fault_address < (unsigned long)(reg_list->list[i].region) + reg_list->list[i].size){
						uffdio_wp.range.start = (unsigned long)(reg_list->list[i].region);
						uffdio_wp.range.len = reg_list->list[i].size;
						uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
						
						if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
							perror("UFFDIO_WRITEPROTECT2");
							exit(EXIT_FAILURE);
						}
					}
				}

				printf("pair_size %d\n", pair_size);
				for(int i = 0; i < pair_size; i++){
					printf("%d\n", i);
					if(fault_address >= (unsigned long)(pair[i].isend_addr) &&
							fault_address < (unsigned long)(pair[i].isend_addr) + pair[i].isend_size){
						printf("try_lock\n");
						if(pthread_mutex_trylock(&(pair[i].pair_lock)) == 0){
							printf("send_addr %p size %u comp_addr %p\n", 
									pair[i].isend_addr, pair[i].isend_size,
									pair[i].comp_addr);
							int comp_size = compress_lz4_buffer(pair[i].isend_addr, pair[i].isend_size,
									pair[i].comp_addr, pair[i].comp_size);

							pair[i].ready = 1;
							printf("comp_size %d\n", comp_size);
							if(comp_size < pair[i].isend_size)
								pair[i].comp_size = comp_size;
							pthread_mutex_unlock(&(pair[i].pair_lock));
						}
					}
				}

			}
		}
	}

	return NULL;
}


void uffd_register(char *addr, size_t size){
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
	}
}



