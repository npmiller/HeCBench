#===============================================================================
# User Options
#===============================================================================

# Compiler can be set below, or via environment variable
CC        = icpx
OPTIMIZE  = yes
DEBUG     = no
DEVICE    = gpu

#===============================================================================
# Program name & source code list
#===============================================================================

program = main

source = main.cpp

obj = $(source:.cpp=.o)

#===============================================================================
# Sets Flags
#===============================================================================

# Standard Flags
CFLAGS := -std=c++14 -Wall -I../ccs-cuda

# Linker Flags
LDFLAGS = 

# Debug Flags
ifeq ($(DEBUG),yes)
  CFLAGS += -g
  LDFLAGS  += -g
endif


# Optimization Flags
ifeq ($(OPTIMIZE),yes)
  CFLAGS += -O3
endif

ifeq ($(DEVICE),gpu)
  CFLAGS +=-fiopenmp -fopenmp-targets=spir64 -D__STRICT_ANSI__
else
  CFLAGS +=-qopenmp
endif
#===============================================================================
# Targets to Build
#===============================================================================

$(program): $(obj)
	$(CC) $(CFLAGS) $(obj) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(program) $(obj)

run: $(program)
	./$(program) -t 0.9 -i ../ccs-cuda/Data_Constant_100_1_bicluster.txt -o ./Output.txt -m 50 -p 1 -g 100.0 -r 100
	diff -Bb Output.txt ../ccs-cuda/ccs_constant_100_1.out
