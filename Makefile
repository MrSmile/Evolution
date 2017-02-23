
HEADER = math.h world.h
SOURCE = world.cpp main.cpp
FLAGS = -std=c++11 -Wall -Wno-parentheses
D_FLAGS = -g -O0 -DDEBUG
R_FLAGS = -g -Ofast -flto -mtune=native -DNDEBUG
LIBS = -lSDL2 -lGL
PROGRAM = evolution


debug: $(SOURCE) $(HEADER)
	g++ $(FLAGS) $(D_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	g++ $(FLAGS) $(R_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

clean:
	rm $(PROGRAM)
