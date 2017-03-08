
CXX = g++
D_FLAGS = -g -O0 -DDEBUG
R_FLAGS = -g -Ofast -flto -mtune=native -DNDEBUG
FLAGS = -std=c++11 -Wall -Wno-parentheses -Ibuild -DBUILTIN
LIBS = -lSDL2 -lGL -lepoxy

SOURCES = math.cpp hash.cpp stream.cpp world.cpp graph.cpp main.cpp
SHADERS = food.vert food.frag creature.vert creature.frag back.vert back.frag gui.vert gui.frag
IMAGES = icon.png gui.png
PROGRAM = evolution

D_DIR = build/debug
R_DIR = build/release
RES_NAME = resource
RES_HDR  = build/$(RES_NAME).h
RES_DATA = build/$(RES_NAME).cpp
D_RES_OBJ = $(D_DIR)/$(RES_NAME).o
R_RES_OBJ = $(R_DIR)/$(RES_NAME).o
RES_PACK = respack

D_OBJECTS = $(patsubst %.cpp, $(D_DIR)/%.o, $(SOURCES)) $(D_RES_OBJ)
R_OBJECTS = $(patsubst %.cpp, $(R_DIR)/%.o, $(SOURCES)) $(R_RES_OBJ)
SHADERS_DIR = $(foreach FILE, $(SHADERS), shaders/$(FILE))
IMAGES_DIR = $(foreach FILE, $(IMAGES), images/$(FILE))

debug: $(D_DIR)/$(PROGRAM)
	cp $(D_DIR)/$(PROGRAM) $(PROGRAM)

release: $(R_DIR)/$(PROGRAM)
	cp $(R_DIR)/$(PROGRAM) $(PROGRAM)

$(D_DIR)/$(PROGRAM): $(RES_HDR) $(D_OBJECTS)
	$(CXX) $(FLAGS) $(D_FLAGS) $(D_OBJECTS) $(LIBS) -o $(D_DIR)/$(PROGRAM)

$(R_DIR)/$(PROGRAM): $(RES_HDR) $(R_OBJECTS)
	$(CXX) $(FLAGS) $(R_FLAGS) $(R_OBJECTS) $(LIBS) -o $(R_DIR)/$(PROGRAM)

$(D_RES_OBJ): $(RES_DATA)
	$(CXX) $(FLAGS) $(D_FLAGS) -c $< -o $@

$(R_RES_OBJ): $(RES_DATA)
	$(CXX) $(FLAGS) $(R_FLAGS) -c $< -o $@

$(D_DIR)/%.o: src/%.cpp
	$(CXX) $(FLAGS) $(D_FLAGS) -c -MMD $< -o $@

$(R_DIR)/%.o: src/%.cpp
	$(CXX) $(FLAGS) $(R_FLAGS) -c -MMD $< -o $@

-include $(D_OBJECTS:%.o=%.d) $(R_OBJECTS:%.o=%.d)

$(RES_HDR) $(RES_DATA): respack $(SHADERS_DIR) $(IMAGES_DIR)
	./$(RES_PACK) $(SHADERS) $(IMAGES)

$(RES_PACK): src/$(RES_PACK).cpp
	$(CXX) -g -std=c++11 -Wall -Wno-parentheses $< -lpnglite -lz -o $@

clean:
	rm -f $(PROGRAM) $(RES_PACK) $(D_DIR)/* $(R_DIR)/* $(RES_HDR) $(RES_DATA)
