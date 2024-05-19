#include "wrapper.h"

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int wrapper_MPI_Isend( const void *buf, int count, MPI_Datatype type, int dest,
	         int tag, MPI_Comm comm, MPI_Request *request )
{
	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	int index = find_and_create((char*)buf, type_size);
	
	if( index != -1 ){
		if( pair[index].comp_size < type_size ){
			if( rank == 0 )
				printf("sending i %d pair_size %d comp_size %d orig_size %d\n",
						index, pair_size, 
						pair[index].comp_size,
						pair[index].isend_size );
			int ret = MPI_Isend( pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
					     dest, tag, comm, request );
			return ret;
		}
	}

	return MPI_Isend( buf, count, type, dest, tag, comm, request );
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
	       int tag, MPI_Comm comm, MPI_Request *request )
{
	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Waitall( int count, MPI_Request array_of_requests[],
	                 MPI_Status *array_of_statuses )
{
	return MPI_Waitall( count, array_of_requests, array_of_statuses );
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );

	pthread_t threadId;
	pthread_create( &threadId, NULL, starts_async_compression, NULL );

	return ret;
}
