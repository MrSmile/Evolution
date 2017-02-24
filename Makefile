
CXX = g++
HEADER = math.h world.h
SOURCE = math.cpp world.cpp main.cpp
FLAGS = -std=c++11 -Wall -Wno-parentheses -DBUILTIN
D_FLAGS = -g -O0 -DDEBUG
R_FLAGS = -g -Ofast -flto -mtune=native -DNDEBUG
LIBS = -lSDL2 -lGL
PROGRAM = evolution


debug: $(SOURCE) $(HEADER)
	$(CXX) $(FLAGS) $(D_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	$(CXX) $(FLAGS) $(R_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM)
