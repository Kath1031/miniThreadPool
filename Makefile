
override objs =  threadpool.o
vpath % include:src

test:  test.o $(objs)
	g++ -std=c++17 -o $@ $^  -lpthread

$(objs): %.o : %.cpp %.h
	g++ -std=c++17 -c $<  -Iinclude

test.o: test.cpp
	g++ -std=c++17 -c test.cpp -Iinclude


.PHONY: clean

clean:
	-rm ${objs}
	-rm test.o
	-rm test