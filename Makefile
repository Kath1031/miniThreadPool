
vpath % include:src

test:  test.o 
	g++ -std=c++17 -o $@ $^  -lpthread


test.o: test.cpp
	g++ -std=c++17 -c test.cpp -Iinclude


.PHONY: clean

clean:
	-rm test.o
	-rm test