CC=mpic++
CFLAGS=-fPIC -Wall
LDFLAGS=-shared          
RM=rm -f                 
TARGET_LIB=libwrapper.so 

SRCS=wrapper.c
OBJS=$(SRCS:.c=.o)
HEADERS=wrapper.h

.PHONY: all clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJS): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(MAIN) $(TARGET_LIB)
