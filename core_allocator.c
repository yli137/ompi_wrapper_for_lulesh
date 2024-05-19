#include "wrapper.h"
#include <stdlib.h>
#include <pthread.h>

#include <stdio.h>
#include <mpi.h>
#include <hwloc.h>

int core_allocator() 
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Initialize the topology object
    hwloc_topology_t topology;
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);

    // Get the current binding
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topology, cpuset, HWLOC_CPUBIND_PROCESS);

    // Find the core where the process is running
    int depth = hwloc_get_type_depth(topology, HWLOC_OBJ_CORE);
    int num_cores = hwloc_get_nbobjs_by_depth(topology, depth);

    for (int i = 0; i < num_cores; i++) {
        hwloc_obj_t core = hwloc_get_obj_by_depth(topology, depth, i);
        if (hwloc_bitmap_intersects(cpuset, core->cpuset)) {
            printf("Rank %d is running on core %d out of %d cores.\n", rank, core->logical_index, num_cores);
            break;
        }
    }

    // Clean up
    hwloc_bitmap_free(cpuset);
    hwloc_topology_destroy(topology);

    return 0;
}

