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
