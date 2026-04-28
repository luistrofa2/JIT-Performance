#!/usr/bin/env bash
set -e

mkdir -p ../Languages/Inputs

java -Xint ../Languages/Java/fasta.java-6/fasta.java 250000 > ../Languages/Inputs/input250000.txt
java -Xint ../Languages/Java/fasta.java-6/fasta.java 1000000 > ../Languages/Inputs/input1000000.txt
java -Xint ../Languages/Java/fasta.java-6/fasta.java 5000000 > ../Languages/Inputs/input5000000.txt
java -Xint ../Languages/Java/fasta.java-6/fasta.java 25000000 > ../Languages/Inputs/input25000000.txt
