#!/bin/bash
export LANG=C.UTF-8

WIFI_INTERFACE="wlp2s0"  # Change this if your Wi-Fi interface has a different name

# Function to restore system settings on exit
cleanup() {
    echo "Cleaning up system settings..."
    if ip link show "$WIFI_INTERFACE" > /dev/null 2>&1; then
        sudo ip link set "$WIFI_INTERFACE" up
        echo "Wi-Fi interface $WIFI_INTERFACE turned on."
    fi
}

# Set trap to call cleanup function on script exit (normal or error)
trap cleanup EXIT

# Turn Wi-Fi interface off
if ip link show "$WIFI_INTERFACE" > /dev/null 2>&1; then
    sudo ip link set "$WIFI_INTERFACE" down
    echo "Wi-Fi interface $WIFI_INTERFACE turned off."
else
    echo "[ERROR] Wi-Fi interface $WIFI_INTERFACE not found."
fi

# List of supported languages - dynamically get all folders in Languages directory
VALID_LANGUAGES=($(find "$(dirname "$0")/../Languages" -maxdepth 1 -type d -exec basename {} \; | grep -v "^Languages$" | sort))

# Function to check if a language is valid
is_valid_language() {
    local lang=$1
    for valid in "${VALID_LANGUAGES[@]}"; do
        if [[ "$lang" == "$valid" ]]; then
            return 0
        fi
    done
    return 1
}

# Function to show help
show_help() {
    echo "Usage: $0 [--perf] [--overtime] <language1> [language2] ... or 'all'"
    echo
    echo "Options:"
    echo "  --perf       Run in perf measurement mode"
    echo "  --overtime   Run in overtime measurement mode"
    echo "  --help, -h   Show this help message"
    echo
    echo "Supported languages: ${VALID_LANGUAGES[*]}"
    exit 0
}

# Ensure at least one argument is provided
if [ "$#" -lt 1 ]; then
    show_help
fi

# Initialize modes
PERF_MODE=0
OVERTIME_MODE=0

# Process options
while [[ "$1" == --* ]] || [[ "$1" == -h ]]; do
    case "$1" in
        --perf)
            PERF_MODE=1
            shift
            ;;
        --overtime)
            OVERTIME_MODE=1
            shift
            ;;
        --help|-h)
            show_help
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            ;;
    esac
done

# Select languages
if [[ "$1" == "all" ]]; then
    SELECTED_LANGUAGES=("${VALID_LANGUAGES[@]}")
else
    SELECTED_LANGUAGES=()
    for lang in "$@"; do
        if is_valid_language "$lang"; then
            SELECTED_LANGUAGES+=("$lang")
        else
            echo "Invalid language: $lang"
            echo "Supported languages: ${VALID_LANGUAGES[*]} or 'all'"
            exit 1
        fi
    done
fi

# Define number of times to execute each program
NTIMES=10

# Define time limit (here it's in hours) for the execution of each program
TIME_OUT_LIMIT=4

# Define the CPU temperature variance 
VARIANCE=5

# Define the number of seconds for the idle execution and for temperature calibration
IDLE_SECONDS=10

# Define interval for overtime measurements (in milliseconds)
INTERVAL=100

# Define power cap values
#POWER_CAPS=(-1 5)
POWER_CAPS=(-1)

# Move to project root
cd "$(dirname "$0")/.." || exit 1

# Build RAPL sensor library
cd RAPL || exit 1
gcc -fPIC -shared -o sensors.so sensors.c
cd ..

# Initialize output CSV
if [[ $PERF_MODE -eq 1 ]]; then
    OUT_CSV="perf_stats.csv"
    echo "Language,Program,Powercap,cycles,instructions,cache-misses,branch-misses,stalled-cycles-frontend,stalled-cycles-backend,resource_stalls.any,mem_trans_retired.load_latency_gt_128,cpu-cycles,uops_executed.stall_cycles" > "$OUT_CSV"
elif [[ $OVERTIME_MODE -eq 1 ]]; then
    OUT_CSV="measurements_ot.csv"
    echo "Language,Program,Powercap,Package,Core,GPU,DRAM,Timestamp,Temperature" > "$OUT_CSV"
else
    OUT_CSV="measurements.csv"
    echo "Language,Program,Powercap,Package,Core,GPU,DRAM,Time,Temperature,Memory" > "$OUT_CSV"
fi

# Run benchmarks for each power cap
for cap in "${POWER_CAPS[@]}"; do
    cd RAPL || exit 1
    make clean
    make
    cd ..

    for lang in "${SELECTED_LANGUAGES[@]}"; do
        lang_dir="Languages/$lang"
        echo "lang: $lang_dir"
        if [ ! -d "$lang_dir" ]; then
            echo "Warning: Language directory not found: $lang_dir"
            continue
        fi

        # Iterate over program directories safely (handles spaces)
        find "$lang_dir" -mindepth 1 -maxdepth 1 -type d | while read -r program_dir; do
            makefile_path="$program_dir/Makefile"
            if [ -f "$makefile_path" ]; then
                echo "Processing $program_dir"
                cd "$program_dir" || { echo "Failed to enter $program_dir"; continue; }

                benchmark_name="$(basename "$program_dir")"
                
                benchmark_name_no_extension="${benchmark_name%%.*}"
                echo "Handling benchmark_name: $benchmark_name"
                echo "Handling benchmark_name_no_extension: $benchmark_name_no_extension"

                if [ $lang == "Java" ]; then
                    make measure program="$benchmark_name" program_name="$benchmark_name_no_extension" n_times=$NTIMES powercap="$cap" time_out_limit=$TIME_OUT_LIMIT variance=$VARIANCE sleep_secs=$IDLE_SECONDS

                    if [ -f "measurements.csv" ]; then
                        tail -n +2 "measurements.csv" >> "../../../$OUT_CSV"
                    else
                        echo "Warning: measurements.csv not found in $lang_dir"
                    fi

                    make measure_Xint program="$benchmark_name" program_name="$benchmark_name_no_extension" n_times=$NTIMES powercap="$cap" time_out_limit=$TIME_OUT_LIMIT variance=$VARIANCE sleep_secs=$IDLE_SECONDS

                    if [ -f "measurements.csv" ]; then
                        tail -n +2 "measurements.csv" >> "../../../$OUT_CSV"
                    else
                        echo "Warning: measurements.csv not found in $lang_dir"
                    fi
                    make measure_XComp program="$benchmark_name" program_name="$benchmark_name_no_extension" n_times=$NTIMES powercap="$cap" time_out_limit=$TIME_OUT_LIMIT variance=$VARIANCE sleep_secs=$IDLE_SECONDS

                    if [ -f "measurements.csv" ]; then
                        tail -n +2 "measurements.csv" >> "../../../$OUT_CSV"
                    else
                        echo "Warning: measurements.csv not found in $lang_dir"
                    fi

                else
                    make compile program="$benchmark_name" program_name="$benchmark_name_no_extension"
                    make measure program="$benchmark_name" program_name="$benchmark_name_no_extension" n_times=$NTIMES powercap="$cap" time_out_limit=$TIME_OUT_LIMIT variance=$VARIANCE sleep_secs=$IDLE_SECONDS

                    if [ -f "measurements.csv" ]; then
                        tail -n +2 "measurements.csv" >> "../../../$OUT_CSV"
                    else
                        echo "Warning: measurements.csv not found in $lang_dir"
                    fi
                    
                    make measure_jit program="$benchmark_name" program_name="$benchmark_name_no_extension" n_times=$NTIMES powercap="$cap" time_out_limit=$TIME_OUT_LIMIT variance=$VARIANCE sleep_secs=$IDLE_SECONDS

                    if [ -f "measurements.csv" ]; then
                        tail -n +2 "measurements.csv" >> "../../../$OUT_CSV"
                    else
                        echo "Warning: measurements.csv not found in $lang_dir"
                    fi
                    make clean
                fi

                cd - > /dev/null

            else
                echo "Makefile not found: $makefile_path"
            fi
        done
    done
done

# Clean RAPL
cd RAPL || exit 1
make clean
cd ..

echo "Measurement process completed."
