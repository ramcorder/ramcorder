CC = gcc
OPT = # -O
DEB = -g

CFLAGS = $(OPT) $(DEB) -DLIBSERIALPORT_ATBUILD
LFLAGS =

DEPS = serailPort.h serialPortInternal.h \
       utils/date.hutils/cLog.h utils/date.h \
       utils/format.h utils/formatColor.h utils/formatCore.h utils/format-inl.h

OBJS = main.o \
       utils/cLog.o utils/format.o \
       serialPort/serialPort.o serialPort/serialTiming.o serialPort/serialLinux.o serialPort/serialLinuxTermios.o

ramcorder: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -lstdc++ -o ramcorder

%.o: %.c  $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

all: ramcorder

rebuild:
	make clean
	make all -j8

.PHONY: clean
clean:
	rm -f -v *.o
	rm -f -v ramcorder
	rm -f -v *.~*
	rm -f -v *.*~
	rm -f -v *~
	rm -f -v .~*
