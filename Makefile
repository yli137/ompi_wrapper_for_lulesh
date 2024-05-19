CC=mpic++
CFLAGS=-fPIC -Wall -g
LDFLAGS=-shared
DEPENDED_LIB=-L/home/yli137/lz4/lib -llz4
RM=rm -f                 
TARGET_LIB=libwrapper.so 

SRCS=wrapper.c tracking.c compression.c core_allocator.c
OBJS=$(SRCS:.c=.o)
HEADERS=wrapper.h

.PHONY: all clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(DEPENDED_LIB)

clean:
	$(RM) $(OBJS) $(MAIN) $(TARGET_LIB)


%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@
