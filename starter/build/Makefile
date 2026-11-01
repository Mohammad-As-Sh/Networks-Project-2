# compiler and Linker settings
CC       = gcc
LINKER   = gcc

# compiling flags here
CFLAGS   = -Wall -g -I../
LFLAGS   = -Wall -g

# for parent directory
SRCDIR = ..

#  names
CLIENT_EXE = rdt_sender
SERVER_EXE = rdt_receiver
EXECUTABLES = $(CLIENT_EXE) $(SERVER_EXE)

# Object files needed for each executable
CLIENT_SOURCES = $(SRCDIR)/rdt_sender.c $(SRCDIR)/common.c $(SRCDIR)/packet.c
SERVER_SOURCES = $(SRCDIR)/rdt_receiver.c $(SRCDIR)/common.c $(SRCDIR)/packet.c

CLIENT_OBJS = $(notdir $(CLIENT_SOURCES:.c=.o))
SERVER_OBJS = $(notdir $(SERVER_SOURCES:.c=.o))
ALL_OBJS    = $(sort $(CLIENT_OBJS) $(SERVER_OBJS))

# default
all: $(EXECUTABLES)

# Rule to link the client executable
$(CLIENT_EXE): $(CLIENT_OBJS)
	@echo "Linking client: $@"
	$(LINKER) $(LFLAGS) $^ -o $@

# Rule to link the server executable
$(SERVER_EXE): $(SERVER_OBJS)
	@echo "Linking server: $@"
	$(LINKER) $(LFLAGS) $^ -o $@

# generic pattern rule for compiling .c files into .o files.
%.o: $(SRCDIR)/%.c
	@echo "Compiling $< -> $@"
	$(CC) $(CFLAGS) -c $< -o $@

# The 'clean' target
# It removes executables and object files from this directory.
# It also removes all files inside 'receiveddata' but leaves the directory itself.
clean:
	@echo "Cleaning object files and executables..."
	@rm -f $(ALL_OBJS) $(EXECUTABLES)
	@echo "Clearing contents of 'receiveddata' directory..."
	@rm -f receiveddata/*
	@echo "Cleanup complete. 'sampledata' and 'receiveddata' folders are preserved."

# A phony target is one that doesn't represent a real file.
.PHONY: all clean
    