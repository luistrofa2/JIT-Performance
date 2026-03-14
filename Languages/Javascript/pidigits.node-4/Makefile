n_times ?= 3
time_out_limit ?= 4
variance ?= 5
sleep_secs ?= 10
powercap ?= -1
interval ?= 100
program ?= binarytrees.js
program_name ?= binarytrees

executable_path ?= node --jitless
executable_path_jit ?= node

language ?= NodeJS
language_jit ?= NodeJS_JIT

INPUT_binarytrees:=21
INPUT_fannkuchredux:=12
INPUT_fasta:=25000000
INPUT_knucleotide:=0 < ../../knucleotide-input25000000.txt
INPUT_mandelbrot:=16000
INPUT_nbody:=50000000
INPUT_pidigits:=10000
INPUT_regexredux:=0 < ../../regexredux-input5000000.txt
INPUT_revcomp:=0 < ../../revcomp-input5000000.txt
INPUT_spectralnorm:=5500

export n_times

# Require file argument
ifndef program
$(error Usage: make measure program=binarytrees.js)
endif

# Extract numeric input from file name (program -> program_input)
input := $(INPUT_$(program_name))

# Paths
SRC_FILE := $(program)
PROGRAM_NAME := $(program_name)

measure:
	echo "Measuring $(program) with input $(input)"
	sudo modprobe msr
	sudo ../../../RAPL/main "$(executable_path) $(program_name).js $(input)" \
		$(language) $(program)_$(input) \
		$(n_times) $(variance) \
		$(time_out_limit) $(sleep_secs) $(powercap)

measure_jit:
	echo "Measuring $(program) with input $(input)"
	sudo modprobe msr
	sudo ../../../RAPL/main "$(executable_path_jit) $(program_name).js $(input)" \
		$(language_jit) $(program)_$(input) \
		$(n_times) $(variance) \
		$(time_out_limit) $(sleep_secs) $(powercap)

compile:

clean:
