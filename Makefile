
CXX = g++
HEADER = math.h hash.h stream.h world.h graph.h shaders.h
SOURCE = math.cpp hash.cpp stream.cpp world.cpp graph.cpp main.cpp
SHADER = food.vert food.frag creature.vert creature.frag
FLAGS = -std=c++11 -Wall -Wno-parentheses -DBUILTIN
D_FLAGS = -g -O0 -DDEBUG
R_FLAGS = -g -Ofast -flto -mtune=native -DNDEBUG
LIBS = -lSDL2 -lGL -lepoxy
PROGRAM = evolution


debug: $(SOURCE) $(HEADER)
	$(CXX) $(FLAGS) $(D_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

release: $(SOURCE) $(HEADER)
	$(CXX) $(FLAGS) $(R_FLAGS) $(SOURCE) $(LIBS) -o $(PROGRAM)

shaders.h: $(foreach FILE, $(SHADER), shaders/$(FILE))
	{ $(foreach FILE, $(SHADER), xxd -i shaders/$(FILE);) } > shaders.h

clean:
	rm $(PROGRAM)
