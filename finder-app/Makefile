ifeq($(CROSS_COMPILE),)
	CC ?=gcc
else
	CC = $(CROSS_COMPILE)gcc
endif

CFLAGS = -Wall -Wextra -O2
TARGET = writer
SRCS = writer.c
OBJS = $(SRCS:.c=.o)    # using substituiton reference

# our default target is "writer"
all: $(TARGET)

# Linking objects into elf file
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compiling C source file into object
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cleaning build artifacts
clean:
	rm -rf $(TARGET) $(OBJS)
