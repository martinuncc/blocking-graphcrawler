CXXFLAGS=-I ~/rapidjson/include
LDFLAGS=-lcurl
LD=g++
CC=g++

all: client blocking


client: client.o
	$(LD) $< -o $@ $(LDFLAGS)

blocking: blocking.o
	$(LD) $< -o $@ $(LDFLAGS)


clean:
	-rm client client.o blocking blocking.o
