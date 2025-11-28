CC=g++
INCLUDE_DIR = ../include
CPPFLAGS = -std=c++11 -fPIC -I$(INCLUDE_DIR)
LDFLAGS = -Wall

SRCS = test_driver.cpp ../src/pef.cpp
OBJS = $(SRCS:.cpp=.o)

TARGET = test_driver
all: $(TARGET)

%.o: %.cpp
	$(CC) $(CPPFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: clean

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)

