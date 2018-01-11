TARGET = tbinpack.dll
LUAINC = -I /usr/local/include
LUALIB = -L /usr/local/bin -llua53
CFLAGS = -Wall -O2
DISABLEWARNINGS = -Wno-parentheses -Wno-unknown-pragmas -Wno-unused-variable -Wno-shift-overflow -Wno-maybe-uninitialized -Wno-unused-but-set-variable

all : $(TARGET)
all : winfile.dll	# only for windows
all : etc2codec.dll

$(TARGET) : tbinpack.c transform.c
	gcc --shared $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB)

winfile.dll : winfile.c
	gcc --shared $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB) -lshell32

etc2codec.dll : etc2codec.cxx etcdec.cxx
	g++ --shared $(CFLAGS) -o $@ $^ $(LUAINC) $(LUALIB) $(DISABLEWARNINGS)

clean :
	rm $(TARGET) winfile.dll etc2codec.dll

