
CXX = g++
D_FLAGS = -g -O0 -DDEBUG
R_FLAGS = -g -Ofast -flto -mtune=native -DNDEBUG
FLAGS = -std=c++11 -Wall -Wno-parentheses -Ibuild -DBUILTIN
LIBS = -lSDL2 -lGL -lepoxy

SOURCES = math.cpp hash.cpp stream.cpp world.cpp graph.cpp main.cpp
SHADERS = food.vert food.frag creature.vert creature.frag
PROGRAM = evolution

D_DIR = build/debug
R_DIR = build/release
RES_HDR = build/shaders.h

D_OBJECTS = $(patsubst %.cpp, $(D_DIR)/%.o, $(SOURCES))
R_OBJECTS = $(patsubst %.cpp, $(R_DIR)/%.o, $(SOURCES))

debug: $(D_DIR)/$(PROGRAM)
	cp $(D_DIR)/$(PROGRAM) $(PROGRAM)

release: $(R_DIR)/$(PROGRAM)
	cp $(R_DIR)/$(PROGRAM) $(PROGRAM)

$(D_DIR)/$(PROGRAM): $(RES_HDR) $(D_OBJECTS)
	$(CXX) $(FLAGS) $(D_FLAGS) $(D_OBJECTS) $(LIBS) -o $(D_DIR)/$(PROGRAM)

$(R_DIR)/$(PROGRAM): $(RES_HDR) $(R_OBJECTS)
	$(CXX) $(FLAGS) $(R_FLAGS) $(R_OBJECTS) $(LIBS) -o $(R_DIR)/$(PROGRAM)

$(D_DIR)/%.o: src/%.cpp
	$(CXX) $(FLAGS) $(D_FLAGS) -c -MMD $< -o $@

$(R_DIR)/%.o: src/%.cpp
	$(CXX) $(FLAGS) $(R_FLAGS) -c -MMD $< -o $@

-include $(D_OBJECTS:%.o=%.d) $(R_OBJECTS:%.o=%.d)

$(RES_HDR): $(foreach FILE, $(SHADERS), shaders/$(FILE))
	{ $(foreach FILE, $(SHADERS), xxd -i shaders/$(FILE);) } > $@

clean:
	rm -f $(PROGRAM) $(D_DIR)/* $(R_DIR)/* $(RES_HDR)
