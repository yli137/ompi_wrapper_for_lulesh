**6/30/2024**

Split the entire compression list into two parts and placed them onto (rank+8) and (rank+24) for doing compression separately

Need to time when compressed buffer stopped changing and record the timestamp.

**10/3/2024**

uffd cannot register overlap region, first glance. 

Used bkmalloc to do mmap, and registration. Have not caught a fault yet.

**10/9/2024**

Register memory is having trouble.

**10/22/2024**

Register memory is done, but not working. Wanted to see previous registration and register remainder of the current address + size.

Fault thread seems not catching anything. Need to check if the data is being modified or not.

Currently, only registering first 10000B+ region for each thread.

**10/23/2024**

need to move fault handler args to global

**11/2/2024**

if memory region is not allocated right before registration. A clear of fault status is needed. Now it can catch fault during lulesh. 

Need to add compression call back after each fault.

**11/5/2024**

registration overlap done. Need to add compression.

uffd need to match in order to register multiple addresses into one handler thread.

Performance is not good. Handling fault may be taking way too long or detection of the fault is way too often.

**11/6/2024**

Addded a usleep after compression. Performance is good only when rank 0 do compression and all other ranks do not. 

Guess is that I need to place thread accordingly.

**11/8/2024**

Compression thread and fault handler thread working separately. Need to merge them.
