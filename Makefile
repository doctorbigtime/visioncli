.PHONY: all clean
all: visioncli pwmd

clean:
	rm -f visioncli pwmd

visioncli: visioncli.cpp
	g++ -std=c++1z $< -o $@ 

pwmd: pwmd.cpp
	g++ -g -std=c++1z $< -o $@ -lboost_filesystem -lboost_system 


