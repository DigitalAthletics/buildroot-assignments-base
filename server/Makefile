ifdef CC
SYSROOT=$(shell $(CC) -print-sysroot)
else
SYSROOT=/
endif

TARGET=aesdsocket

default: 
	$(CC) -g -Wall -c $(TARGET).c -o $(TARGET).o 
	$(CC) -g -Wall $(TARGET).o -o $(TARGET) 
# $(CC) -g -Wall -I$(SYSROOT) $(TARGET).o -o $(TARGET) 

all: default

clean: 
	rm -f $(TARGET).o
	rm -f $(TARGET)
