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

	for( int i = 0; i < pair_size; i++ ){
		if( pair[i].isend_addr == addr && pair[i].isend_size == size ){
			return i;
		}
	}

	if( pair_size == -1 ){
		pair = (Pair*)malloc(sizeof(Pair));

		pair[0].isend_addr = addr;
		pair[0].isend_size = size;
		pair[0].comp_addr = (char*)malloc(size + 100);
		pair[0].comp_size = size + 100;
		pair[0].created = -1;

		pair_size = 1;
		pthread_mutex_unlock( &creation_lock );
		return -1;
	} else {
		pair = (Pair*)realloc(pair, sizeof(Pair) * (pair_size+1));

		pair[pair_size].isend_addr = addr;
		pair[pair_size].isend_size = size;
		pair[pair_size].comp_addr = (char*)malloc(size+100);
		pair[pair_size].comp_size = size + 100;
		pair[pair_size].created = -1;

		pair_size++;
		pthread_mutex_unlock( &creation_lock );
		return -1;
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


reg_addr_list *init_register_list()
{
	reg_addr_list *register_list = (reg_addr_list*)malloc(sizeof(reg_addr_list));

	register_list->size = 30;
	register_list->pos = 0;
	register_list->list = (reg_addr*)malloc(sizeof(reg_addr) * 30);

	return register_list;
}

reg_addr_list *realloc_register_list()
{
	reg_list->size *= 2;
	reg_list->list = (reg_addr*)realloc(reg_list->list, 
			sizeof(reg_addr) * reg_list->size);

	return reg_list;
}

void add_reg_pair(char *region, int size)
{
	unsigned long point = (unsigned long)region + size;
	char *add_point = NULL;
	int add_size = -1;
	for(int i = 0; i < reg_list->pos; i++){
		if(point >= (unsigned long)(reg_list->list[i].region) &&
				point < (unsigned long)(reg_list->list[i].region) + reg_list->list[i].size){
			add_point = reg_list->list[i].region + reg_list->list[i].size;
			add_size = size - reg_list[i].size;
		}
	}

	if(add_size > 0 && add_point != NULL){
		if(reg_list->pos == reg_list->size)
			reg_list = realloc_register_list();

		reg_list->list[reg_list->pos].region = add_point;
		reg_list->list[reg_list->pos].size = add_size;
		reg_list->pos++;
	}
}
