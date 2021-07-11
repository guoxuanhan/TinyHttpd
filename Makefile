all: myhttp

myhttp: httpd.cpp
	g++ httpd.cpp -o myhttp -std=c++11 -lpthread

clean:
	rm myhttp
