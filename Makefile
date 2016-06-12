exec = bvh-browser

OBJDIR = obj
CFLAGS =  -g -Wall -Isrc $(shell sdl-config --cflags)
LDFLAGS = $(shell sdl-config --libs) -lGL -lm

headers = $(wildcard src/*.h src/*/*.h)
sources = $(wildcard src/*.cpp src/*/*.cpp)
objects = $(addprefix $(OBJDIR)/, $(sources:.cpp=.o))
dirs    = $(dir $(objects))

# Colour coding of g++ output - highlights errors and warnings
SED = sed -e 's/error/\x1b[31;1merror\x1b[0m/g' -e 's/warning/\x1b[33;1mwarning\x1b[0m/g'
SED2 = sed -e 's/undefined reference/\x1b[31;1mundefined reference\x1b[0m/g'

ifeq ($(OS),Windows_NT)
#LDFLAGS += -lgdi32 -lopengl32 -static-libgcc -static-libstdc++
LDFLAGS += -DWIN32
else
CFLAGS += -DLINUX
endif

.PHONY: clean

all: $(exec)

$(exec): $(objects)
	@echo -e "\033[34;1m[ Linking ]\033[0m"
	@echo $(CXX) -o $(exec) $(objects) $(CFLAGS) $(LDFLAGS)
	@$(CXX) -o $(exec) $(objects) $(CFLAGS) $(LDFLAGS) 2>&1 | $(SED2)

$(OBJDIR)/%.o: %.cpp $(headers) | $(OBJDIR)
	@echo $<
	@$(CXX) $(CFLAGS) -c $< -o $@ 2>&1 | $(SED)
	
$(OBJDIR):
	mkdir -p $(dirs);

clean:
	rm -f *~ */*~ $(exec)
	rm -rf $(OBJDIR)

