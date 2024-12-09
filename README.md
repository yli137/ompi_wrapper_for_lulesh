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

**11/12/2024**

Lock might run into some issue, race condition? but wasnt sure. 

I think address lookup needs some work. Address might be within a region but it does not belong to the pair.

Try adding more compression threads to the other two sets of cores, each handle one region.

Currently, one thread can only handle one large region at this point.

Might also need to check how much decompression cost, it is possible that decomrpession outweighs the performance gain from reduced size during communication

**12/9/2024**

multi thread compression, default 106s -> compression 104s.

Need to place compression threads on 8-15 and 24-31.

There is some issue with catching fault and doing compression. Some region are never caught. Is it not modified or matched to "early" pais in the pair list?
