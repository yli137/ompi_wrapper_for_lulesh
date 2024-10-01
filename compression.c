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
	int num = *((int*)arg);

	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	if( num == 0 )
		CPU_SET( rank + 8, &cpuset );
	else	
		CPU_SET( rank + 24, &cpuset );
	int s = pthread_setaffinity_np( pthread_self(), sizeof(cpu_set_t), &cpuset );
	if( s != 0 )
		fprintf(stderr, "rank %d did not place the thread\n", rank);

	int pair_ret, creation_ret, got_pair_size,
	    comp_ret;
	while(1){
		creation_ret = pthread_mutex_trylock( &creation_lock );

		if( creation_ret == 0 ){
			got_pair_size = pair_size;
			int locks[got_pair_size];
			pthread_mutex_unlock( &creation_lock );

			int start, end;
			if( num == 0 ){
				start = 0; 
				end = got_pair_size/2;
			} else {
				start = got_pair_size/2;
				end = got_pair_size;

				if( start == 0 )
					continue;
			}

			for( int i = start; i < end; i++ )
				if( pair[i].isend_size > 1000 )
					locks[i] = pthread_mutex_trylock( &(pair[i].pair_lock) );

			for( int i = start; i < end; i++ ){
				if( pair[i].isend_size > 1000 ){
					if( locks[i] == 0 && pair[i].ready != 1 ){
						comp_ret = compress_lz4_buffer( pair[i].isend_addr, pair[i].isend_size,
								pair[i].comp_addr, pair[i].comp_size );
						pair[i].comp_size = comp_ret != 0 ? comp_ret: pair[i].comp_size;
						
						pair[i].ready = 1;
						pthread_mutex_unlock( &(pair[i].pair_lock) );
					} else if( locks[i] == 0 && pair[i].ready == 1 ){
						pthread_mutex_unlock( &(pair[i].pair_lock) );
					}
				}
			}
		}
	}
}
