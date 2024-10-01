#include "wrapper.h"
#include <stdlib.h>
#include <pthread.h>

#include <stdio.h>
#include <mpi.h>

Pair *pair;
int pair_size = -1;

pthread_mutex_t creation_lock;

int find_and_create( char *addr, int size )
{

	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	int lock_ret;
	for( int i = 0; i < pair_size; i++ ){
		if( pair[i].isend_addr == addr && pair[i].isend_size == size ){
			if( pair[i].isend_size > 1000 ){
				return i;
			} 

			return -1;
		}
	}

	if( pair_size == -1 ){
		pair = (Pair*)malloc(sizeof(Pair));

		pair[0].isend_addr = addr;
		pair[0].isend_size = size;
		pair[0].comp_addr = (char*)malloc(size + 100);
		pair[0].comp_size = size + 100;
		pair[0].ready = -1;
		pair[0].sending = 0;

		pair_size = 1;
		pthread_mutex_unlock( &creation_lock );
		return -1;
	} else {
		pair = (Pair*)realloc(pair, sizeof(Pair) * (pair_size+1));

		pair[pair_size].isend_addr = addr;
		pair[pair_size].isend_size = size;
		pair[pair_size].comp_addr = (char*)malloc(size+100);
		pair[pair_size].comp_size = size + 100;
		pair[pair_size].ready = -1;
		pair[pair_size].sending = 0;

		pair_size++;
		pthread_mutex_unlock( &creation_lock );
		return -1;
	}
}

void hint_compression_starts()
{
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	for( int i = 0; i < pair_size; i++ ){
		pair[i].ready = -1;
		pthread_mutex_unlock( &(pair[i].pair_lock) );
	}
}

void hint_compression_starts7_13()
{
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	for( int i = 7; i < 14; i++ ){
		pair[i].ready = -1;
		pthread_mutex_unlock( &(pair[i].pair_lock) );
	}
}

void hint_compression_starts14_16()
{
	int rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &rank );

	for( int i = 14; i < 17; i++ ){
		pair[i].ready = -1;
		pthread_mutex_unlock( &(pair[i].pair_lock) );
	}
}

// Function to initialize the recv_manager structure
void recv_manager_init(recv_manager_t *manager) {
	manager->size = 0;
	manager->capacity = INITIAL_CAPACITY;
	manager->recv_addrs = (char**)malloc(manager->capacity * sizeof(char*));
	manager->requests = (MPI_Request**)malloc(manager->capacity * sizeof(MPI_Request*));
	manager->tag = (int*)malloc(manager->capacity * sizeof(int));

	for(int i = 0; i < manager->capacity; i++)
		manager->recv_addrs[i] = NULL;

	if (manager->recv_addrs == NULL || manager->requests == NULL) {
		printf("------------Failed to allocate memory for recv_manager\n");
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
}

// Function to add a new MPI_Irecv to the list
void recv_manager_add(recv_manager_t *manager, void *recv_addr, int tag, MPI_Request *request) {
	// Check if we need to resize the list
	if (manager->size >= manager->capacity){
		manager->capacity *= 2;
		manager->recv_addrs = (char**) realloc(manager->recv_addrs, manager->capacity * sizeof(char*));
		manager->requests = (MPI_Request**) realloc(manager->requests, manager->capacity * sizeof(MPI_Request*));
		manager->tag = (int*) realloc(manager->tag, manager->capacity * sizeof(int));

		if (manager->recv_addrs == NULL || manager->requests == NULL) {
			printf("--------------Failed to reallocate memory for recv_manager\n");
			MPI_Abort(MPI_COMM_WORLD, 1);
		}
	}

	// Add the new receiving address and request
	manager->recv_addrs[manager->size] = (char*)recv_addr;
	manager->requests[manager->size] = request;
	manager->tag[manager->size] = tag;

	manager->size++;

}

// Function to free the recv_manager resources
void recv_manager_free(recv_manager_t *manager) {
	free(manager->recv_addrs);
	free(manager->requests);
	free(manager->tag);
	manager->recv_addrs = NULL;
	manager->requests = NULL;
	manager->size = 0;
	manager->capacity = 0;
}
