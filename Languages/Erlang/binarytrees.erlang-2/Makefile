n_times ?= 3
time_out_limit ?= 4
variance ?= 5
sleep_secs ?= 10
powercap ?= -1
interval ?= 100
program ?= binarytrees.erl
program_name ?= binarytrees

executable_path ?= otp_src_27.0_no_jit/bin/erl
executable_path_jit ?= otp_src_27.0/bin/erl

language ?= Erlang
language_jit ?= Erlang_JIT

INPUT_binarytrees:=21
INPUT_fannkuchredux:=12
INPUT_fasta:=25000000
INPUT_knucleotide:=0 < ../../Inputs/knucleotide-input250000.txt
INPUT_mandelbrot:=16000
INPUT_nbody:=50000000
INPUT_pidigits:=10000
INPUT_regexredux:=0 < ../../Inputs/regexredux-input5000000.txt
INPUT_revcomp:=0 < ../../Inputs/revcomp-input250000.txt
INPUT_spectralnorm:=5500

export n_times

# Require file argument
ifndef program
$(error Usage: make measure program=binarytrees)
endif

# Extract numeric input from file name (program -> program_input)
input := $(INPUT_$(program_name))

# Paths
SRC_FILE := $(program)
PROGRAM_NAME := $(program_name)

compile:
	$(executable_path)c $(program_name).erl

measure:
	echo "Measuring $(program) with input $(input)"
	sudo modprobe msr
	sudo ../../../RAPL/main "$(executable_path) -smp enable -noshell -noinput -run $(program_name) main $(input)" \
		$(language) $(program)_$(input) \
		$(n_times) $(variance) \
		$(time_out_limit) $(sleep_secs) $(powercap)

measure_jit:
	echo "Measuring $(program) with input $(input)"
	sudo modprobe msr
	sudo ../../../RAPL/main "$(executable_path_jit) -smp enable -noshell -noinput -run $(program_name) main $(input)" \
		$(language_jit) $(program)_$(input) \
		$(n_times) $(variance) \
		$(time_out_limit) $(sleep_secs) $(powercap)

clean:
	rm -f *.beam