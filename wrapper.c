#include "wrapper.h"

#include <mpi.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>


#include <linux/userfaultfd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

static volatile int stop;
static int first = -1;

static inline uint64_t getns(void)
{
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	assert(ret == 0);
	return (((uint64_t)ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;
}

static long get_page_size(void)
{
	long ret = sysconf(_SC_PAGESIZE);
	if (ret == -1) {
		perror("sysconf/pagesize");
		exit(1);
	}
	assert(ret > 0);
	return ret;
}

static void *handler(void *arg)
{
	struct fault_handler_args* fargs = (struct fault_handler_args*) arg;
	struct uffd_msg msg;
	ssize_t nread;

	struct pollfd pollfd;
	pollfd.fd = fargs->uffd;
	pollfd.events = POLLIN;

	while (poll(&pollfd, 1, -1) > 0) {
		printf("trying to read\n\n");
		nread = read(fargs->uffd, &msg, sizeof(msg));
		if (nread == 0 || nread == -1) {
			perror("Error reading userfaultfd event");
			continue;
		}

		if (msg.event == UFFD_EVENT_PAGEFAULT) {
			if (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
				printf("Caught a write protection fault!\n");

				// To resolve the fault, allow the write by removing write protection
				mprotect(fargs->address, fargs->length, PROT_READ | PROT_WRITE);
			}
		}
	}

	return NULL;
}


int wrapper_MPI_Isend( const void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	long page_size = sysconf(_SC_PAGESIZE);
	int index = find_and_create((char*)buf, type_size);

	if(rank == 0 && index == -1 && type_size > 10000){
		printf("coming in\n\n");
		first = 0;

		// Step 1: Create a userfaultfd object
		int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
		assert(uffd != -1);

		// Step 2: Register the memory with userfaultfd
		struct uffdio_api uffdio_api;
		uffdio_api.api = UFFD_API;
		uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
		assert(ioctl(uffd, UFFDIO_API, &uffdio_api) != -1);

		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (unsigned long)buf;
		uffdio_register.range.len = ((type_size + page_size - 1) / page_size) * page_size;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP; 

		assert(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) != -1);

		// Step 4: Spawn a thread to handle page faults
		pthread_t uffd_thread;
		struct fault_handler_args args;
		args.uffd = uffd;
		args.length = ((type_size + page_size - 1) / page_size) * page_size;
		args.address = (void*)buf;

		assert(pthread_create(&uffd_thread, NULL, handler, &args) == 0);

	}

	return MPI_Isend( buf, count, type, dest, tag, comm, request );
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status)
{
	return MPI_Wait(request, status);
}

int wrapper_MPI_Waitall( int count, MPI_Request array_of_requests[],
		MPI_Status *array_of_statuses )
{
	return MPI_Waitall(count, array_of_requests, array_of_statuses);
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );

	return ret;
}
