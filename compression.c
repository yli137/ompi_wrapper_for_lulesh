#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <mpi.h>
#include <lz4.h>

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

	if( decomp_size > 0 )
		memcpy( srcAddr, decompress_buffer, decomp_size );

	free( decompress_buffer );
	return 1;
}

void *starts_async_compression(void*)
{
#if 0 // this is where core allocator starts
	core_allocator();
#endif 
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	int pair_ret, creation_ret, got_pair_size,
	    comp_ret;
	while(1){
		creation_ret = pthread_mutex_trylock( &creation_lock );

		if( creation_ret == 0 ){
			got_pair_size = pair_size;
			pthread_mutex_unlock( &creation_lock );

			for( int i = 0; i < got_pair_size; i++ ){
				if( pair[i].isend_size > 1000 ){
					pair_ret = pthread_mutex_trylock( &(pair[i].pair_lock) );

					if( pair_ret == 0 && pair[i].ready != 1 ){
						comp_ret = compress_lz4_buffer( pair[i].isend_addr, pair[i].isend_size,
								pair[i].comp_addr, pair[i].comp_size );
						if( rank == 0 )
						printf("i %d pair_size %d comp_size %d orig_size %d\n",
								i, got_pair_size, comp_ret, pair[i].isend_size);
						
						comp_ret != 0 ? pair[i].comp_size = comp_ret : pair[i].comp_size = pair[i].comp_size;
						pair[i].ready = 1;
						pthread_mutex_unlock( &(pair[i].pair_lock) );
					} else if( pair_ret == 0 && pair[i].ready == 1 ){
						pthread_mutex_unlock( &(pair[i].pair_lock) );
					}
				}
			}
		}
	}

}