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

int try_decompress( MPI_Request *request, MPI_Status *status, char *srcAddr )
{

	int recv_count = 0;
	MPI_Get_count( status, MPI_BYTE, &recv_count );

	char *decompress_buffer = (char*)malloc( recv_count * 270 );
	int decomp_size = decompress_lz4_buffer_default(
			(const char*)srcAddr,
			recv_count,
			decompress_buffer,
			recv_count * 270 );

	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	//if( decomp_size > 0 && rank == 0 ) printf("DECOMPRESSION rank %d recv %d actual_size %d\n",
	//		rank, recv_count, decomp_size);
	if( decomp_size > 0 )
		memcpy( srcAddr, decompress_buffer, decomp_size );

	free( decompress_buffer );
	return 1;
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
