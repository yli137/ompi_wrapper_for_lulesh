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

#include <sched.h>
#include <hwloc.h>

#include "wrapper.h"

void *handler(void *arg)
{
	struct fault_handler_args* fargs = (struct fault_handler_args*) arg;
	struct uffd_msg msg;
	ssize_t nread;
	
	hwloc_topology_t topology;
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	hwloc_bitmap_t cpuset = hwloc_bitmap_alloc();
	hwloc_bitmap_zero(cpuset);
	hwloc_bitmap_set(cpuset, fargs->rank + 8);
	if (hwloc_set_thread_cpubind(topology, pthread_self(), cpuset, HWLOC_CPUBIND_THREAD) != 0) {
		perror("hwloc_set_thread_cpubind failed");
		exit(EXIT_FAILURE);
	}
	

	struct pollfd pollfd;
	pollfd.fd = fargs->uffd;
	pollfd.events = POLLIN;

	int page_size = sysconf(_SC_PAGE_SIZE);

	while(poll(&pollfd, 1, -1) > 0){
		nread = read(fargs->uffd, &msg, sizeof(msg));
		if (nread == 0 || nread == -1) 
			continue;

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
				double last_fault = MPI_Wtime();

				struct uffdio_writeprotect uffdio_wp;
				for(int i = 0; i < pair_size; i++){
					if(fault_address >= (unsigned long)(pair[i].aligned_addr) &&
							fault_address < (unsigned long)(pair[i].aligned_addr) + pair[i].aligned_size){
						pthread_mutex_lock(&(pair[i].pair_lock));
						pair[i].ready = 0;
						pair[i].faults++;
						pair[i].last_fault = last_fault;

						pthread_mutex_lock(&cache_lock);
						put(cache, (unsigned long)(pair[i].isend_addr) % (size_t)(pair[i].isend_size), (size_t)(pair[i].isend_size));
						
						pthread_mutex_unlock(&cache_lock);

						pthread_mutex_unlock(&(pair[i].pair_lock));
					}
				}

				
				for(int i = 0; i < reg_list->pos; i++){
					if( pthread_mutex_lock(&(reg_list->list[i].reg_lock)) == 0 ){
						uffdio_wp.range.start = (unsigned long)(reg_list->list[i].region);
						uffdio_wp.range.len = reg_list->list[i].size;
						uffdio_wp.mode = 0;

						if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
							perror("UFFDIO_WRITEPROTECT2");
							exit(EXIT_FAILURE);
						}
						reg_list->list[i].dirty = 1;
						pthread_mutex_unlock(&(reg_list->list[i].reg_lock));
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
		char *region = reg_list->list[reg_list->pos].region;
		size_t region_size = reg_list->list[reg_list->pos].size;

		int uffd = fargs->uffd;
		assert(uffd != -1);

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

		//uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
		//assert(ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) != -1);

		reg_list->pos++;
	}
}



