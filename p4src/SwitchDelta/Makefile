CC=ccache gcc -std=c99
CXX=ccache g++ -std=c++11
FLAGS= -I/root/bf-sde-8.8.1/install/include -I../ -Igen-cpp -I. -fPIC -DPIC -O2
SRC=$(wildcard thrift-src/*.cpp gen-cpp/*.cpp)
OBJS=$(patsubst %.cpp, %.o, $(SRC))
PD_C=src/pd.c

LIBTH=libth.so
LIBPD=libpd.so

$(LIBTH):$(OBJS)
	$(CXX) $(FLAGS) -shared -o $@ $^

%.o:%.cpp
	$(CXX) $(FLAGS) -o $@ -c $<

$(LIBPD):$(PD_C)
	$(CC) $(FLAGS) -shared -o $@ $^

all:$(LIBPD) $(LIBTH)
	echo ""

clean:
	rm -rf $(LIBPD) $(LIBTH) $(OBJS)
