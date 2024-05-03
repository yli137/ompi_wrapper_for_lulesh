#ifndef WRAPPER_H
#define WRAPPER_H

#include <mpi.h>

char *try_isend( const void *buf, int count, MPI_Datatype type, int dest,
	         int tag, MPI_Comm comm, MPI_Request *request );

int try_irecv( void *buf, int count, MPI_Datatype type, int source,
	       int tag, MPI_Comm comm, MPI_Request *request );

#endif
