#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <mpi.h>
#include <lz4.h>

#include <sched.h>
#include <hwloc.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <omp.h>

#include <stdint.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <fcntl.h>
#include <poll.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>

int last_comp_index = 0;

int compress_lz4_buffer( const char *input_buffer, int input_size,
		char *output_buffer, int output_size )
{
	return LZ4_compress_default( input_buffer, output_buffer, input_size, output_size );
}


int decompress_lz4_buffer_default( const char *input_buffer, int input_size,
		char *output_buffer, int output_size )
{
	return LZ4_decompress_safe( input_buffer, output_buffer, input_size, output_size );
}

void try_decompress( char *input_buffer, int input_size )
{
	int output_size = input_size * 1000;
	char *decompressed_buffer = (char*)malloc(output_size);
	int dsize = decompress_lz4_buffer_default(input_buffer, input_size, decompressed_buffer, output_size);

	if(dsize > input_size)
		memcpy(input_buffer, decompressed_buffer, dsize);

	free(decompressed_buffer);
}

void *starts_async_compression(void *arg)
{
	Node *node;
	comp_thread_args cargs = *((comp_thread_args*)arg);
	
	hwloc_topology_t topology;
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	hwloc_bitmap_t cpuset = hwloc_bitmap_alloc();
	hwloc_bitmap_zero(cpuset);
	hwloc_bitmap_set(cpuset, cargs.rank + 24);
	if (hwloc_set_thread_cpubind(topology, pthread_self(), cpuset, HWLOC_CPUBIND_THREAD) != 0) {
		perror("hwloc_set_thread_cpubind failed");
		exit(EXIT_FAILURE);
	}
	

	struct uffdio_writeprotect uffdio_wp;
	while(1){
		//usleep(1);

		pthread_mutex_lock(&cache_lock);
		if(cache->size > 0){
			node = remove_lru(cache);
			//printf("rank %d catch one\n", cargs.rank);
		}
		pthread_mutex_unlock(&cache_lock);

		if(node != NULL){
			for(int i = 0; i < pair_size; i++){
				if(node->key == (unsigned long)(pair[i].isend_addr) % (size_t)(pair[i].isend_size) && node->value == (size_t)(pair[i].isend_size)){
					if(pthread_mutex_lock(&(pair[i].pair_lock)) == 0){
						if(cargs.rank == PRINT_RANK)
							printf("comp got pair_lock %d\n", i);
						// setting pair to be compressed ready
						pair[i].comp_size = pair[i].isend_size+100;
						pair[i].ready = 1;
						pair[i].thread = 1;
						pthread_mutex_unlock(&(pair[i].pair_lock));
						if(cargs.rank == PRINT_RANK)
							printf("comp released pair_lock %d\n", i);
					}
				}
			}

			for(int j = 0; j < reg_list->pos; j++){
				if( pthread_mutex_lock(&(reg_list->list[j].reg_lock)) == 0 ){
					if(reg_list->list[j].dirty == 1){

						//printf("rank %d locking up %d pos %d\n", cargs.rank, j, reg_list->pos);
						uffdio_wp.range.start = (unsigned long)(reg_list->list[j].region);
						uffdio_wp.range.len = reg_list->list[j].size;
						uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

						if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
							perror("UFFDIO_WRITEPROTECT2");
							exit(EXIT_FAILURE);
						}
						reg_list->list[j].dirty = 0;
					}
					pthread_mutex_unlock(&(reg_list->list[j].reg_lock));
				}
			}

			for(int i = 0; i < pair_size; i++){
				if(node->key == (unsigned long)(pair[i].isend_addr) % (size_t)(pair[i].isend_size) && node->value == (size_t)(pair[i].isend_size)){
					if(pthread_mutex_trylock(&(pair[i].pair_lock)) == 0){
						
						if(cargs.rank == PRINT_RANK)
							printf("comp got pair_lock %d\n", i);

						if(pair[i].sending == 0){
							pthread_mutex_unlock(&(pair[i].pair_lock));
							if(cargs.rank == PRINT_RANK)
								printf("comp released pair_lock %d\n", i);
							int comp_size = compress_lz4_buffer(pair[i].isend_addr, pair[i].isend_size,
									pair[i].comp_addr, pair[i].isend_size + 100);

							pthread_mutex_lock(&(pair[i].pair_lock));
							if(cargs.rank == PRINT_RANK)
								printf("comp done got pair_lock %d\n", i);
							if(comp_size < pair[i].isend_size && comp_size != 0){
								pair[i].comp_size = comp_size;
								//pair[i].ready = 1;
								pair[i].thread = 1;

								last_comp_index = i;
							}
						}

						pair[i].ncomp++;
						pthread_mutex_unlock(&(pair[i].pair_lock));
						
						if(cargs.rank == PRINT_RANK)
							printf("comp released pair_lock %d\n", i);
					}
				}
			}

			node = NULL;
		}
	}
}
