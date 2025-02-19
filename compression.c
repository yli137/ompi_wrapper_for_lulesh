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

long long decomp_time = 0;
long long total_decomp = 0;

int compress_lz4_buffer( const char *input_buffer, int input_size,
		char *output_buffer, int output_size )
{
	return LZ4_compress_default( input_buffer, output_buffer, input_size, output_size );
}


int decompress_lz4_buffer_default( const char *input_buffer, int input_size,
		char *output_buffer, int output_size )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int ret = LZ4_decompress_safe( input_buffer, output_buffer, input_size, output_size );
	
	if (ret < 0) {
		fprintf(stderr, "LZ4 decompression failed! Error code: %d input_size %d output_size %d\n", 
				ret, input_size, output_size);
		exit(EXIT_FAILURE);
	} 

	return ret;
}

void try_decompress( char *input_buffer, int input_size, int supposed_recv_size )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if(input_size != supposed_recv_size){

		char *decompressed_buffer = (char*)malloc(supposed_recv_size);
		int dsize = decompress_lz4_buffer_default(input_buffer, input_size, decompressed_buffer, supposed_recv_size);

		if(dsize != supposed_recv_size && dsize > 0)
			printf("rank %d not decompressed right dsize %d input_size %d supposed_size %d\n", rank, dsize, input_size, supposed_recv_size);

		memcpy(input_buffer, decompressed_buffer, dsize);

		free(decompressed_buffer);
	}
}

void *starts_async_compression(void *arg)
{
	Node *node = NULL;
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
#define SKIP_TIME 0.0001
		pthread_mutex_lock(&cache_lock);
		if(cache->size > 0){
			while(get_timestamp() - get_first_node_time(cache) < SKIP_TIME){
				pthread_mutex_unlock(&cache_lock);
				usleep(10);
				pthread_mutex_lock(&cache_lock);
			}

			node = remove_lru(cache);
		}
		pthread_mutex_unlock(&cache_lock);

		if(node != NULL){
			unsigned long pair_st = 0,
				      pair_ed = 0,
				      reg_st,
				      reg_ed;

			for(int i = 0; i < pair_size; i++){
				if(node->key == (unsigned long)(pair[i].isend_addr) % (size_t)(pair[i].isend_size) && node->value == (size_t)(pair[i].isend_size)){
					if(pthread_mutex_lock(&(pair[i].pair_lock)) == 0){
						pair[i].comp_size = pair[i].isend_size+100;
						pair[i].ready = 1;
						pair[i].thread = 1;

						pair_st = (unsigned long)(pair[i].aligned_addr);
						pair_ed = (unsigned long)(pair[i].aligned_addr) + pair[i].aligned_size;

						pthread_mutex_unlock(&(pair[i].pair_lock));
					}
				}
			}

			pthread_mutex_lock(&reg_lock);
			for(int j = 0; j < reg_list->pos; j++){
				reg_st = (unsigned long)(reg_list->list[j].region);
				reg_ed = (unsigned long)(reg_list->list[j].region) + reg_list->list[j].size;

				if(pair_st != 0 && pair_ed != 0){

					if((pair_st >= reg_st && pair_st <= reg_ed ) || (pair_ed >= reg_st && pair_ed <= reg_ed)){
						if(reg_list->list[j].dirty == 1){
							uffdio_wp.range.start = (unsigned long)(reg_list->list[j].region);
							uffdio_wp.range.len = reg_list->list[j].size;
							uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

							if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
								perror("UFFDIO_WRITEPROTECT2");
								exit(EXIT_FAILURE);
							}

							reg_list->list[j].dirty = 0;
						}

					}
				}
			}
			pthread_mutex_unlock(&reg_lock);

			for(int i = 0; i < pair_size; i++){

				if(node->key == (unsigned long)(pair[i].isend_addr) % (size_t)(pair[i].isend_size) && node->value == (size_t)(pair[i].isend_size)){
					int comp_size = compress_lz4_buffer(pair[i].isend_addr, pair[i].isend_size,
							pair[i].comp_addr, pair[i].isend_size + 100);

					if(pthread_mutex_trylock(&(pair[i].pair_lock)) == 0){
						if(comp_size < pair[i].isend_size && comp_size != 0 && pair[i].sending != 1 ){
							//printf("rank %d ready size %d ready %d\n", cargs.rank, comp_size, pair[i].ready);
							pair[i].comp_size = comp_size;
							pair[i].thread = 1;

							last_comp_index = i;
						}

						pair[i].ncomp++;
						pthread_mutex_unlock(&(pair[i].pair_lock));
					}
				}
			}

			node = NULL;
			pair_st = 0;
			pair_ed = 0;
		}
	}
}
