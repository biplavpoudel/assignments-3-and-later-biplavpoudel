ifeq ($(CROSS_COMPILE),)
	CC ?= gcc
else
	CC ?= $(CROSS_COMPILE)gcc
endif

CFLAGS ?= -g -Wall -Wextra -O2
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

SRCS = aesdsocket.c
OBJS = $(SRCS:.c=.o)

.PHONY: all default clean

all default: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Linking $(OBJS) into $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiling $< into $@"

clean:
	rm -rf $(TARGET) $(OBJS)
	@echo "Cleaning up $(TARGET) and $(OBJS)"
