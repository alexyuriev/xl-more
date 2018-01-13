X11HOME = /usr/X11R6

CC	= gcc
CFLAGS	= -O2 -Wall -I${X11HOME}/include
LIBS	= -L${X11HOME}/lib -lX11

# Uncomment for systems with libcrypt
LIBS   += -lpam

# uncomment if you are running under solaris
#LIBS   += -lnsl -lsocket

OBJS = main.o

all:	xl-more

xl-more: $(OBJS)
	$(CC) $(CFLAGS) -o xl-more $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS)

realclean: clean
	rm -f xl-more
