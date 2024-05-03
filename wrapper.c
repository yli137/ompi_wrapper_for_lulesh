#include <mpi.h>


char *try_isend( const void *buf, int count, MPI_Datatype type, int dest,
	         int tag, MPI_Comm comm, MPI_Request *request )
{
	MPI_Isend( buf, count, type, dest, tag, comm, request );
	return NULL;
}


int try_irecv( void *buf, int count, MPI_Datatype type, int source,
	       int tag, MPI_Comm comm, MPI_Request *request )
{
	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}
