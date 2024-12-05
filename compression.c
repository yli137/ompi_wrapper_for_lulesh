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
	int do_wait = 0;
	while(1){
		pthread_mutex_lock(&creation_lock);
		for(int i = 0; i < pair_size; i++){
			// acquire pair lock
			if(pair[i].ready == 0){
				do_wait = 1;
				// setup write protect
				struct uffdio_writeprotect uffdio_wp;

				for(int j = 0; j < reg_list->pos; j++){
					if((unsigned long)(pair[i].isend_addr) >= (unsigned long)(reg_list->list[j].region) && 
							(unsigned long)(pair[j].isend_addr) < (unsigned long)(reg_list->list[j].region) + reg_list->list[j].size && pair[i].ready == 0){
						uffdio_wp.range.start = (unsigned long)(reg_list->list[j].region);
						uffdio_wp.range.len = reg_list->list[j].size;
						uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

						if (ioctl(fargs->uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
							perror("UFFDIO_WRITEPROTECT2");
							exit(EXIT_FAILURE);
						}

						int comp_size = compress_lz4_buffer(pair[i].isend_addr, pair[i].isend_size,
								pair[i].comp_addr, pair[i].comp_size);

						if(comp_size < pair[i].isend_size){
							pair[i].comp_size = comp_size;
							pair[i].ready = 1;
						}
						//printf("Reset WP i %d ready %d size %d\n", i, pair[i].ready, pair[i].isend_size);
					}
				}

			}
		}
		pthread_mutex_unlock(&creation_lock);
		if(do_wait == 1){
			usleep(1);
			do_wait = 0;
		}
	}
}
