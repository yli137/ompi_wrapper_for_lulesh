#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <mpi.h>
#include <lz4.h>

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
	comp_thread_args cargs = *((comp_thread_args*)arg);

	int did_comp = 0;
	while(1){
		usleep(10);
		struct uffdio_writeprotect uffdio_wp;

		//pthread_mutex_lock(&reg_lock);
		for(int j = 0; j < reg_list->pos; j++){
			//if(cargs.rank == 0)
			//	printf("grab reg %d dirty %d iter %d\n", j, reg_list->list[j].dirty, did_comp);
			if(reg_list->list[j].dirty == 1){
				//if(cargs.rank == 0)
				//	printf("WP on %d pos %d\n", j, reg_list->pos);

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
		//pthread_mutex_unlock(&reg_lock);

		for(int i = 0; i < pair_size; i++){
			// try do lock differently
			if( pthread_mutex_trylock(&(pair[i].pair_lock)) == 0 ){
				//if(cargs.rank == 0)
				//	printf("grab lock ready %d iter %d\n", pair[i].ready, did_comp++);
				if(pair[i].ready == 0){
					//int comp_size = compress_lz4_buffer(pair[i].isend_addr, pair[i].isend_size,
					//		pair[i].comp_addr, pair[i].comp_size);

					pair[i].ready = 1;
					//if(comp_size < pair[i].isend_size)
					//	pair[i].comp_size = comp_size;
				}
				pthread_mutex_unlock(&(pair[i].pair_lock));
			}
		}
	}
}
