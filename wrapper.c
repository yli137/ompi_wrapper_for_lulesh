#include "wrapper.h"

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>

int first_encounter = 1;

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
			
			//printf("rank %d sending i %d to %d pair_size %d comp_size %d orig_size %d\n",
			//		rank, index, dest, pair_size, 
			//		pair[index].comp_size,
			//		pair[index].isend_size );

			clock_gettime( CLOCK_REALTIME, &(pair[index].ts) );

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
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	pthread_mutex_lock( &creation_lock );
	for( int i = 0; i < pair_size; i++ ){
		int ret = pthread_mutex_trylock( &(pair[i].pair_lock) );
	}
	pthread_mutex_unlock( &creation_lock );
	return MPI_Waitall( count, array_of_requests, array_of_statuses );
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );

	pthread_t threadId, threadId2;
	int *t1 = (int*)malloc(sizeof(int)),
	    *t2 = (int*)malloc(sizeof(int));
	*t1 = 0;
	*t2 = 1;

	pthread_create( &threadId, NULL, starts_async_compression, t1 );
	pthread_create( &threadId2, NULL, starts_async_compression, t2 );

#if 0
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(rank, &cpuset);
#endif
	return ret;
}
