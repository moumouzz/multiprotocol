CC = mipsel-openwrt-linux-gcc
#CC = gcc
#CC = arm-linux-gcc
TARGET = FF1_SEND
OBJS = duoprotocol.o open62541.o
#CFLAGS = -D_REENTRANT -DDEBUG -DDEBUG_PRINT -g -Wall
CFLAGS = -D_REENTRANT -DDEBUGON -g -Wall -std=c99 -I/home/champer/trunk/staging_dir/target-mipsel_24kec+dsp_musl-1.1.10/usr/include -L/home/champer/trunk/staging_dir/target-mipsel_24kec+dsp_musl-1.1.10/usr/lib
RM = rm -f

$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) -lpthread -lmodbus
$(OBJS):%.o:%.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	$(RM) $(TARGET) $(OBJS)
