CC = gcc
CFLAGS = -W -Wall
TARGET = submitter
OBJECTS = submitter.o submitter_main.o
INC = -I../include

$(TARGET) : $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(INC)
	rm -rf $(OBJECTS)

debug:
	$(CC) $(CFLAGS) -o $(TARGET) submitter.c submitter_main.c $(INC) -DDEBUG
	rm -rf $(OBJECTS)

.c.o :
	$(CC) -c $(INC) $(CFLAGS) $<

clean:
	rm -rf $(OBJECTS) $(TARGET)

submitter.o : submitter.c
submitter_main.o : submitter_main.c
