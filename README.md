**6/30/2024**

Split the entire compression list into two parts and placed them onto (rank+8) and (rank+24) for doing compression separately

Need to time when compressed buffer stopped changing and record the timestamp.

**10/3/2024**

uffd cannot register overlap region, first glance. 

Used bkmalloc to do mmap, and registration. Have not caught a fault yet.
