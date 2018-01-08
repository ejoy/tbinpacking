TARGET = tbinpack.dll
LUAINC = -I /usr/local/include
LUALIB = -L /usr/local/bin -llua53
CFLAGS = -Wall -O2

all : $(TARGET)
all : winfile.dll	# only for windows

$(TARGET) : tbinpack.c
	gcc --shared $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB)

winfile.dll : winfile.c
	gcc --shared $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB) -lshell32

clean :
	rm $(TARGET) winfile.dll

