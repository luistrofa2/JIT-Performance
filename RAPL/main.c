#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <powercap/powercap.h>
#include <raplcap/raplcap.h>
#include <math.h>
#include "rapl.h"
#include "sensors.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <regex.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Forward declarations */
void performMeasurements(const char *command, const char *language, const char *program, int ntimes, int core, const char* MEASUREMENTS_FILE, int POWERCAP, int TIME_OUT_LIMIT, float TEMPERATURETHRESHOLD, int VARIANCE, int memory, int idle);

/* Over-time mode context (used when --over-time passed) */
struct OverTimeCtx {
    volatile int run_flag;
    long usecs;
    char language[128];
    char program[128];
    float baseline_temperature;
    int temperature_variance;
    int powercap; /* -1 = no cap */
    int calib_secs;
};

void runOverTimeMode(struct OverTimeCtx *ctx, const char *command);
void *runRaplLogger(void *arg);

/* Thread return storage */
struct ThreadData {
    int returnValue;
};

static struct ThreadData threadData;

/* Optional: handle signals to cleanup resources gracefully (extend as needed) */
static volatile sig_atomic_t global_terminate = 0;
static void sigint_handler(int signum) {
    (void)signum;
    global_terminate = 1;
}

/* Initialize RAPL caps with POWERCAP; returns 0 on success, -1 on failure */
int initializeRapl(raplcap *rc, int POWERCAP){
    raplcap_limit rl_short, rl_long;
    uint32_t q, j, n, d;

    if (raplcap_init(rc)) {
        perror("raplcap_init");
        return -1;
    }

    n = raplcap_get_num_packages(NULL);
    if (n == 0) {
        perror("raplcap_get_num_packages");
        raplcap_destroy(rc);
        return -1;
    }

    d = raplcap_get_num_die(rc, 0);
    if (d == 0) {
        perror("raplcap_get_num_die");
        raplcap_destroy(rc);
        return -1;
    }

    rl_long.watts = POWERCAP;
    rl_long.seconds = 0;
    rl_short.watts = POWERCAP;
    rl_short.seconds = 0;

    for (q = 0; q < n; q++) {
        for (j = 0; j < d; j++) {
            if (raplcap_pd_set_limits(rc, q, j, RAPLCAP_ZONE_PACKAGE, &rl_long, &rl_short)) {
                perror("raplcap_pd_set_limits");
                /* continue trying others */
            }
        }
    }

    for (q = 0; q < n; q++) {
        for (j = 0; j < d; j++) {
            if (raplcap_pd_set_zone_enabled(rc, q, j, RAPLCAP_ZONE_PACKAGE, 1)) {
                perror("raplcap_pd_set_zone_enabled");
            }
        }
    }
    return 0;
}

/* Safely run a system command */
void runCommand(const char *command) {
    int status = system(command);
    if (status == -1) {
        fprintf(stderr, "[ERROR] Error executing command: system() returned -1\n");
        exit(EXIT_FAILURE);
    }
}

/* Worker to verify program exits 0 */
void* programWorks(void *arg) {
    const char *command = (const char *)arg;
    FILE *fp2 = popen(command, "r");
    if (fp2 == NULL) {
        fprintf(stderr, "[ERROR] Error running program verification (popen)\n");
        threadData.returnValue = 0;
        pthread_exit(NULL);
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp2) != NULL) {
        /* drop output */
    }
    int status = pclose(fp2);
    if (status == -1) {
        fprintf(stderr, "[ERROR] pclose failed\n");
        threadData.returnValue = 0;
        pthread_exit(NULL);
    }

    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);
        if (exit_status == 0) {
            printf("[INFO] Command executed successfully\n");
            threadData.returnValue = 1;
        } else {
            printf("[ERROR] Command exited with an error status: %d\n", exit_status);
            threadData.returnValue = 0;
        }
    } else {
        printf("[ERROR] Command did not exit normally\n");
        threadData.returnValue = 0;
    }
    pthread_exit(NULL);
}

/* Write error sentinel into measurements file */
void writeErrorMessage(const char *language, const char *program, const char *errorMsg, const char* MEASUREMENTS_FILE, int POWERCAP){
    FILE *fp = fopen(MEASUREMENTS_FILE, "w");
    if (fp == NULL) {
        perror("[ERROR] Error opening measurements file");
        exit(-1);
    }

    /* Keep a simple CSV row indicating error */
    fprintf(fp, "%s, %s, %d, %s, %s, %s, %s, %s, %s, %s\n",
            language, program, POWERCAP,
            errorMsg, errorMsg, errorMsg, errorMsg, errorMsg, errorMsg, errorMsg);
    fclose(fp);
}

/* runTesting: verify program runs within timeout then measure (uses programWorks thread) */
void runTesting(int ntimes, int core, const char *command, const char *language, const char *program, const char* MEASUREMENTS_FILE,
                int POWERCAP, int TIME_OUT_LIMIT, float TEMPERATURETHRESHOLD, int VARIANCE, int memory, int idle) {

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("[ERROR] clock_gettime");
        return;
    }

    ts.tv_sec += TIME_OUT_LIMIT;

    pthread_t thread;
    threadData.returnValue = 0; /* safe default */

    int rc = pthread_create(&thread, NULL, programWorks, (void *)command);
    if (rc != 0) {
        fprintf(stderr, "[ERROR] pthread_create(programWorks) failed: %s\n", strerror(rc));
        writeErrorMessage(language, program, "ThreadCreateError", MEASUREMENTS_FILE, POWERCAP);
        return;
    }

    int state = pthread_timedjoin_np(thread, NULL, &ts);
    if (state != 0) {
        if (state == ETIMEDOUT) {
            writeErrorMessage(language, program, "TimeOut", MEASUREMENTS_FILE, POWERCAP);
            printf("[WARNING] Timeout: Function execution interrupted after %d seconds\n", TIME_OUT_LIMIT);
        } else {
            fprintf(stderr, "[ERROR] pthread_timedjoin_np failed: %s\n", strerror(state));
            writeErrorMessage(language, program, "JoinError", MEASUREMENTS_FILE, POWERCAP);
        }
        return;
    }

    if (threadData.returnValue == 1) {
        performMeasurements(command, language, program, ntimes, core, MEASUREMENTS_FILE, POWERCAP, TIME_OUT_LIMIT, TEMPERATURETHRESHOLD, VARIANCE, memory, idle);
    } else {
        writeErrorMessage(language, program, "ProgramError", MEASUREMENTS_FILE, POWERCAP);
    }
}

/* Argument structure for measure thread */
struct MeasureArgs {
    FILE *fp;
    int core;
    const char *command;
    int memory;
    float temperature;
};

void* measure(void *arg) {
    struct MeasureArgs *args = (struct MeasureArgs *)arg;
    double time_spent;
    struct timeval tvb, tva;
    char str_temp[20];

    /* initialize RAPL for the specified core; blocking until success */
    while (rapl_init(args->core) != 0) {
        if (global_terminate) { free(args); pthread_exit(NULL); }
        sleep(1);
    }

    while (1) {
        if (rapl_before(args->fp, args->core) == -1) {
            while (rapl_init(args->core) != 0) {
                if (global_terminate) { free(args); pthread_exit(NULL); }
                sleep(1);
            }
            continue;
        }

        gettimeofday(&tvb, NULL);
        runCommand(args->command);
        gettimeofday(&tva, NULL);

        if (rapl_after(args->fp, args->core) == -1) {
            while (rapl_init(args->core) != 0) {
                if (global_terminate) { free(args); pthread_exit(NULL); }
                sleep(1);
            }
            continue;
        }

        break;
    }

    time_spent = ((tva.tv_sec - tvb.tv_sec) * 1000000 + tva.tv_usec - tvb.tv_usec) / 1000.0;
    snprintf(str_temp, sizeof(str_temp), "%.1f", args->temperature);
    fprintf(args->fp, "%G, %s, %d\n", time_spent, str_temp, args->memory);
    fflush(args->fp);

    /* free the arguments struct */
    free(args);
    return NULL;
}

/* performMeasurements: orchestrates multiple measure runs and writes CSV header when needed */
void performMeasurements(const char *command, const char *language, const char *program, int ntimes, int core, const char* MEASUREMENTS_FILE,
                         int POWERCAP, int TIME_OUT_LIMIT, float TEMPERATURETHRESHOLD, int VARIANCE, int memory, int idle) {
    struct timespec ts;
    FILE *fp;
    float temperature;

    if (idle == 0) {
        fp = fopen(MEASUREMENTS_FILE, "a");
        if (fp == NULL) {
            perror("[ERROR] Error opening measurements file for appending");
            exit(-1);
        }
    } else {
        fp = fopen(MEASUREMENTS_FILE, "w");
        if (fp == NULL) {
            perror("[ERROR] Error opening measurements file for writing");
            exit(-1);
        }
        fprintf(fp, "Language,Program,Powercap,Package,Core,GPU,DRAM,Time,Temperature,Memory\n");
    }

    for (int i = 0; i < ntimes; i++) {
        if (idle == 1) {
            fprintf(fp, "%s, idle_%s, %d, ", language, program, POWERCAP);
        } else {
            fprintf(fp, "%s, %s, %d, ", language, program, POWERCAP);
        }

        temperature = getTemperature();

        while (temperature > TEMPERATURETHRESHOLD + VARIANCE) {
            printf("[INFO] Sleeping 1 second... Current temperature: %.1f°C | Reference temperature: %.1f°C + Variance: %d°C | Waiting for temperature to drop below reference...\n",
                   temperature, TEMPERATURETHRESHOLD, VARIANCE);
            sleep(1);
            temperature = getTemperature();
        }

        if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
            perror("[ERROR] clock_gettime");
            fclose(fp);
            return;
        }

        ts.tv_sec += TIME_OUT_LIMIT;

        pthread_t thread;
        struct MeasureArgs* myArgs = (struct MeasureArgs*)malloc(sizeof(struct MeasureArgs));
        if (!myArgs) {
            perror("[ERROR] malloc MeasureArgs");
            fclose(fp);
            return;
        }
        myArgs->command = command;
        myArgs->core = core;
        myArgs->fp = fp;
        myArgs->temperature = temperature;
        myArgs->memory = memory;

        int rc = pthread_create(&thread, NULL, measure, (void *)myArgs);
        if (rc != 0) {
            fprintf(stderr, "[ERROR] pthread_create(measure) failed: %s\n", strerror(rc));
            free(myArgs);
            fclose(fp);
            return;
        }

        int state = pthread_timedjoin_np(thread, NULL, &ts);
        if (state != 0) {
            if (state == ETIMEDOUT) {
                writeErrorMessage(language, program, "TimeOut", MEASUREMENTS_FILE, POWERCAP);
                printf("[WARNING] Timeout: Function execution interrupted after %d seconds\n", TIME_OUT_LIMIT);
            } else {
                fprintf(stderr, "[ERROR] pthread_timedjoin_np failed: %s\n", strerror(state));
                writeErrorMessage(language, program, "JoinError", MEASUREMENTS_FILE, POWERCAP);
            }
            fclose(fp);
            return;
        }
    }

    fclose(fp);
}

/* measureMemoryUsage: returns maximum resident set size in KB or -1 on error */
int measureMemoryUsage(const char *command) {
    printf("[INFO] Measuring memory consumption of the program \"%s\"\n", command);
    char cmd[2048];

    int n = snprintf(cmd, sizeof(cmd),
            "/usr/bin/time -v %s 2>&1 1>/dev/null | grep 'Maximum resident set size' | "
            "sed 's/[^0-9]*\\([0-9]*\\).*/\\1/'",
            command);
    if (n < 0 || n >= (int)sizeof(cmd)) {
        fprintf(stderr, "[ERROR] command too long for buffer\n");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "Error running command to measure memory\n");
        return -1;
    }

    char buf[128];
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        fprintf(stderr, "Failed to read memory usage\n");
        return -1;
    }

    pclose(fp);
    int result = atoi(buf);
    printf("[INFO] Finished measuring memory consumption of the program \"%s\": %d KB\n", command, result);

    return result;
}

/* calibrateTemperature: sleeps then parses sensors output, returns average temperature */
double calibrateTemperature(int NUMBER_OF_SECONDS) {
    printf("[INFO] Sleeping %d seconds to calibrate the temperature...\n", NUMBER_OF_SECONDS);
    sleep(NUMBER_OF_SECONDS);

    if (system("sensors > /tmp/cores_temperatures.txt") != 0) {
        fprintf(stderr, "[ERROR] Error executing sensors\n");
        return -1;
    }

    FILE *file = fopen("/tmp/cores_temperatures.txt", "r");
    if (file == NULL) {
        perror("[ERROR] Error opening /tmp/cores_temperatures.txt");
        return -1;
    }

    FILE *output_file = fopen("/tmp/cores_temperatures.txt", "a");
    if (output_file == NULL) {
        perror("[ERROR] Error opening /tmp/cores_temperatures.txt for append");
        fclose(file);
        return -1;
    }

    char line[256];
    double temperatures[256];
    int temp_count = 0;
    regex_t regex;
    regmatch_t matches[2];

    if (regcomp(&regex, "Core [0-9]+: *\\+([0-9.]+)°C", REG_EXTENDED) != 0) {
        fprintf(stderr, "[ERROR] regcomp failed\n");
        fclose(file); fclose(output_file);
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        if (regexec(&regex, line, 2, matches, 0) == 0) {
            char temp_str[32];
            int len = matches[1].rm_eo - matches[1].rm_so;
            if (len >= (int)sizeof(temp_str)) len = sizeof(temp_str)-1;
            strncpy(temp_str, line + matches[1].rm_so, len);
            temp_str[len] = '\0';
            double temperature = atof(temp_str);
            if (temp_count < (int)(sizeof(temperatures)/sizeof(temperatures[0]))) {
                temperatures[temp_count++] = temperature;
            }
            /* Extract core number safely */
            char copyline[256];
            strncpy(copyline, line, sizeof(copyline)-1);
            copyline[sizeof(copyline)-1] = '\0';
            char *core_number = strtok(copyline, ":");
            if (core_number) {
                fprintf(output_file, "%s: %.1f°C\n", core_number, temperature);
            }
        }
    }

    double mean_temp = 0.0;
    if (temp_count > 0) {
        for (int i = 0; i < temp_count; i++) mean_temp += temperatures[i];
        mean_temp /= temp_count;
    }

    for (int i = 0; i < temp_count; i++) {
        printf("Core %d temperature: %.1f°C\n", i, temperatures[i]);
    }

    fprintf(output_file, "Average temperature: %.1f°C\n\n", mean_temp);
    regfree(&regex);
    fclose(file);
    fclose(output_file);

    return mean_temp;
}

/* Calibrate powercap across a range, appending results to a CSV and processing them */
void calibratePowercap(int ntimes, int core, char* command, char* language, char* program, int min_powercap, int max_powercap, int POWERCAP,
                       int TIME_OUT_LIMIT, float TEMPERATURETHRESHOLD, int VARIANCE) {
    printf("[INFO] Powercap calibration: Calibrating power cap by running %s (%s) with different power limits...\n", program, language);

    FILE *outputFile = fopen("../Utils/powercap_calibration.csv", "w");
    if (outputFile == NULL) {
        perror("[ERROR] Powercap calibration: Error opening powercap_calibration.csv for writing");
        return;
    }
    fprintf(outputFile, "Language,Program,Powercap,Package,Core,GPU,DRAM,Time,Temperature,Memory\n");

    int memory = measureMemoryUsage(command);

    for (int power_cap = min_powercap; power_cap <= max_powercap; power_cap++) {
        POWERCAP = power_cap;
        printf("[INFO] Powercap calibration: Testing with POWERCAP = %d\n", power_cap);

        raplcap rc;
        if (initializeRapl(&rc, POWERCAP) != 0) {
            printf("[ERROR] Powercap calibration: Failed to initialize RAPL with power cap %d. Skipping...\n", power_cap);
            continue;
        }

        char filename[256];
        snprintf(filename, sizeof(filename), "%s_%d.csv", program, power_cap);

        runTesting(ntimes, core, command, language, program, filename, POWERCAP, TIME_OUT_LIMIT, TEMPERATURETHRESHOLD, VARIANCE, memory, 0);

        FILE *inputFile = fopen(filename, "r");
        if (inputFile == NULL) {
            perror("[ERROR] Powercap calibration: Error opening individual CSV file for reading");
            if (raplcap_destroy(&rc) != 0) {
                perror("[ERROR] Powercap calibration: raplcap_destroy failed");
            }
            continue;
        }

        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), inputFile) != NULL) {
            fputs(buffer, outputFile);
        }
        fclose(inputFile);

        if (remove(filename) != 0) {
            perror("[ERROR] Powercap calibration: Error removing individual CSV file");
        }

        if (raplcap_destroy(&rc) != 0) {
            perror("[ERROR] Powercap calibration: Error destroying CAP");
        }

        sleep(1);
    }

    fclose(outputFile);

    int ret = system("cd .. && python3 Utils/processPowercapCalibrationCSV.py Utils/powercap_calibration.csv");
    if (ret == -1 || WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "[ERROR] Unable to process powercap calibration CSV\n");
    } else {
        printf("[INFO] Power cap calibration CSV processed successfully\n");
    }
}

/* Logger thread that samples rapl values while ctx->run_flag is true */
void *runRaplLogger(void *arg) {
    struct OverTimeCtx *ctx = (struct OverTimeCtx *)arg;
    int core = 0;
    raplcap rc;

    const char *measurements_path = "measurements_ot.csv";
    FILE *mot = fopen(measurements_path, "a+");
    if (!mot) {
        perror("[ERROR] opening measurements_ot.csv");
        pthread_exit(NULL);
    }

    if (initializeRapl(&rc, ctx->powercap) != 0) {
        fprintf(stderr, "[ERROR] runRaplLogger: initializeRapl failed\n");
        fclose(mot);
        pthread_exit(NULL);
    }

    if (fseek(mot, 0, SEEK_END) == 0) {
        long mpos = ftell(mot);
        if (mpos == 0) {
            fprintf(mot, "Language,Program,Powercap,Package,Core,GPU,DRAM,Timestamp,Temperature\n");
            fflush(mot);
        }
    }

    while (rapl_init(core) != 0) {
        if (global_terminate) { fclose(mot); raplcap_destroy(&rc); pthread_exit(NULL); }
        sleep(1);
    }

    long sample_idx = 0;
    long interval_ms = (ctx->usecs + 500) / 1000;

    while (ctx->run_flag && !global_terminate) {
        char *mem = NULL;
        size_t mem_sz = 0;
        FILE *mfp = open_memstream(&mem, &mem_sz);
        if (!mfp) {
            perror("[ERROR] open_memstream");
            break;
        }

        if (rapl_before(mfp, core) == -1) {
            fclose(mfp); free(mem);
            while (rapl_init(core) != 0) {
                if (global_terminate) break;
                sleep(1);
            }
            continue;
        }

        fflush(mfp);
        usleep(ctx->usecs);

        if (ctx->run_flag && !global_terminate) {
            if (rapl_after(mfp, core) == -1) {
                fclose(mfp); free(mem);
                while (rapl_init(core) != 0) {
                    if (global_terminate) break;
                    sleep(1);
                }
                continue;
            }

            fflush(mfp);

            if (mem && mem_sz > 0) {
                char *lastnl = mem_sz ? strrchr(mem, '\n') : NULL;
                char *line = lastnl ? lastnl + 1 : mem;
                size_t len = line ? strlen(line) : 0;
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[--len] = '\0'; }
                if (len > 0) {
                    char pkg[128] = "";
                    char core_s[128] = "";
                    char gpu[128] = "";
                    char dram[128] = "";
                    char tmp[1024];
                    strncpy(tmp, line, sizeof(tmp)-1);
                    tmp[sizeof(tmp)-1] = '\0';
                    char *t = strtok(tmp, ",");
                    if (t) strncpy(pkg, t, sizeof(pkg)-1);
                    t = strtok(NULL, ","); if (t) strncpy(core_s, t, sizeof(core_s)-1);
                    t = strtok(NULL, ","); if (t) strncpy(gpu, t, sizeof(gpu)-1);
                    t = strtok(NULL, ","); if (t) strncpy(dram, t, sizeof(dram)-1);

                    long elapsed_ms = sample_idx * interval_ms;
                    char timebuf[64];
                    snprintf(timebuf, sizeof(timebuf), "%ld", elapsed_ms);

                    double temperature = getTemperature();
                    char tempbuf[32]; snprintf(tempbuf, sizeof(tempbuf), "%.2f", temperature);

                    const char *lang = ctx->language[0] ? ctx->language : "";
                    const char *prog = ctx->program[0] ? ctx->program : "";
                    int powercap_val = ctx->powercap;

                    fprintf(mot, "%s,%s,%d,%s,%s,%s,%s,%s,%s\n",
                            lang, prog, powercap_val,
                            pkg, core_s, gpu, dram,
                            timebuf, tempbuf);
                    fflush(mot);

                    sample_idx++;
                }
            }
        }

        fclose(mfp);
        free(mem);
    }

    fclose(mot);
    if (raplcap_destroy(&rc) != 0) {
        perror("[ERROR] raplcap_destroy in runRaplLogger");
    }
    pthread_exit(NULL);
    return NULL;
}

/* Over-time mode runner */
void runOverTimeMode(struct OverTimeCtx *ctx, const char *command) {


    pthread_t raplThread;
    ctx->run_flag = 1;
    if (pthread_create(&raplThread, NULL, runRaplLogger, ctx) != 0) {
        perror("[ERROR] pthread_create(runRaplLogger)");
        ctx->run_flag = 0;
        return;
    }

    runCommand(command);

    ctx->run_flag = 0;
    pthread_join(raplThread, NULL);

    printf("[INFO] over-time session finished\n");
}

int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    char command[500];
    char language[500] = "";
    char program[500] = "";
    int n_times = 1;
    int powercap = -1;
    int core = 0;
    int sleep_time = 0;
    float temperature_threshold = 0.0f;
    raplcap rc;
    int variance = 0;
    int time_out_limit = 0;

    /* detect --over-time */
    int over_idx = -1;
    for (int i = 1; i < argc; ++i) if (strcmp(argv[i], "--over-time") == 0) { over_idx = i; break; }

    if (over_idx != -1) {
        int powercap_arg = -1;
        int calib_secs_arg = 30;
        int variance_arg = 0;

        char *skip = calloc(argc, 1);
        if (!skip) { perror("calloc"); return -1; }
        skip[0] = 1;
        skip[over_idx] = 1;

        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-powercap") == 0 && i + 1 < argc) {
                powercap_arg = atoi(argv[i + 1]);
                if (powercap_arg < -1 || powercap_arg == 0) {
                    fprintf(stderr, "[ERROR] invalid powercap: %s (use -1 for no cap or >=1)\n", argv[i + 1]);
                    free(skip); return -1;
                }
                skip[i] = 1; skip[i+1] = 1; i++;
                continue;
            }
            if (strcmp(argv[i], "-sleep-secs") == 0 && i + 1 < argc) {
                calib_secs_arg = atoi(argv[i + 1]);
                if (calib_secs_arg <= 0) {
                    fprintf(stderr, "[ERROR] invalid sleep-secs: %s (must be > 0)\n", argv[i + 1]);
                    free(skip); return -1;
                }
                skip[i] = 1; skip[i+1] = 1; i++;
                continue;
            }
            if (strcmp(argv[i], "-variance") == 0 && i + 1 < argc) {
                variance_arg = atoi(argv[i + 1]);
                if (variance_arg < 0) {
                    fprintf(stderr, "[ERROR] invalid variance: %s (must be >= 0)\n", argv[i + 1]);
                    free(skip); return -1;
                }
                skip[i] = 1; skip[i+1] = 1; i++;
                continue;
            }
        }

        const char *pos[argc];
        int posc = 0;
        for (int i = 1; i < argc; ++i) if (!skip[i]) pos[posc++] = argv[i];
        free(skip);

        if (posc < 4) {
            fprintf(stderr, "[ERROR] Usage: %s <command> <interval-ms> <language> <program> --over-time [-powercap X] [-sleep-secs Y] [-variance Z]\n", argv[0]);
            return -1;
        }

        char cli_command[500];
        char cli_language[256] = "";
        char cli_program[256] = "";
        long interval_ms = 0;
        float baseline_temp = 0.0;

        strncpy(cli_command, pos[0], sizeof(cli_command)-1); cli_command[sizeof(cli_command)-1] = '\0';
        interval_ms = atol(pos[1]);
        strncpy(cli_language, pos[2], sizeof(cli_language)-1); cli_language[sizeof(cli_language)-1] = '\0';
        strncpy(cli_program, pos[3], sizeof(cli_program)-1); cli_program[sizeof(cli_program)-1] = '\0';

        if (interval_ms <= 0) { fprintf(stderr, "[ERROR] interval-ms must be a positive integer\n"); return -1; }

        if (access("/tmp/cores_temperatures.txt", F_OK) != 0) {
            printf("[WARNING] Over time session: File /tmp/cores_temperatures.txt not found. Running temperature calibration...\n");
            baseline_temp = calibrateTemperature(calib_secs_arg);
        } else {
            printf("[INFO] Over time session: File /tmp/cores_temperatures.txt found. Skipping temperature calibration.\n");
            baseline_temp = getInitialTemperature();
        }

        struct OverTimeCtx ctx;
        ctx.run_flag = 0;
        ctx.usecs = interval_ms * 1000;
        ctx.powercap = powercap_arg;
        ctx.calib_secs = calib_secs_arg;
        ctx.baseline_temperature = baseline_temp;
        ctx.temperature_variance = variance_arg;
        strncpy(ctx.language, cli_language, sizeof(ctx.language)-1); ctx.language[sizeof(ctx.language)-1] = '\0';
        strncpy(ctx.program, cli_program, sizeof(ctx.program)-1); ctx.program[sizeof(ctx.program)-1] = '\0';

        char powercap_str[32];
        if (ctx.powercap == -1)
            snprintf(powercap_str, sizeof(powercap_str), "not used");
        else
            snprintf(powercap_str, sizeof(powercap_str), "%d W", ctx.powercap);
            
        printf("[INFO] Starting in RAPL over-time mode (single session).\n"
               "       Command: %s\n"
               "       Interval: %ld ms\n"
               "       Language: %s\n"
               "       Program: %s\n"
               "       Powercap: %s\n"
               "       Variance: %d\n",
               cli_command, interval_ms, ctx.language, ctx.program,
               powercap_str, ctx.temperature_variance);



        float temperature = getTemperature();

        while (temperature > ctx.baseline_temperature + ctx.temperature_variance) {
            printf("[INFO] Sleeping 1 second... Current temperature: %.1f°C | Reference temperature: %.1f°C + Variance: %d°C | Waiting for temperature to drop below reference...\n",
                   temperature, ctx.baseline_temperature, ctx.temperature_variance);
            sleep(1);
            temperature = getTemperature();
        }

        runOverTimeMode(&ctx, cli_command);
        return 0;
    }

    /* --help handling etc. (kept as in original) */
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("\nUsage:\n");
        printf("  %s --temperature-calibration <seconds>\n", argv[0]);
        printf("      Calibrates CPU temperature\n\n");

        printf("  %s --powercap-calibration <min>-<max> <variance> <time_out_limit(min)> <n_times> <secs_to_sleep> [compile_command] <run_command> <language> <program_name>\n", argv[0]);
        printf("      Runs a powercap calibration between <min> and <max> watts\n\n");

        printf("  %s <command> <language> <program> <n_times> <variance> <time_out_limit(hrs)> <sleep_time(sec)> <powercap>\n", argv[0]);
        printf("      Standard measurement mode.\n\n");

        printf("  %s \"<command>\" <interval-ms> <language> <program> --over-time [-powercap X] [-sleep-secs Y] [-variance Z]\n", argv[0]);
        printf("      Overtime measurement mode. Optional arguments:\n");
        printf("          -powercap X     Apply a power cap (use -1 for no cap)\n");
        printf("          -sleep-secs Y   Calibration delay before starting in seconds (default 30)\n");
        printf("          -variance Z     Temperature tolerance in °C (default 0)\n\n");

        printf("Examples:\n");
        printf("  %s ./my_program C myprog 3 2 1 60 -1\n", argv[0]);
        printf("  %s --temperature-calibration 20\n", argv[0]);
        printf("  %s --powercap-calibration 40-60 3 10 3 30 gcc myprog.c ./myprog C myprog\n", argv[0]);
        printf("  %s \"./myprog\" 1000 C myprog --over-time -powercap 45 -sleep-secs 20 -variance 3\n", argv[0]);
        return 0;
    }


    if (argc == 3 && strcmp(argv[1], "--temperature-calibration") == 0) {
        int sleepTime = atoi(argv[2]);
        calibrateTemperature(sleepTime);
        return 0;
    }

    if ((argc == 11 || argc == 10) && strcmp(argv[1], "--powercap-calibration") == 0) {
        int min_powercap, max_powercap, secs_to_sleep, n_times;
        char compile_command[500] = "";
        char run_command[500] = "";
        char lang[500] = "";
        char program_name[500] = "";

        if (sscanf(argv[2], "%d-%d", &min_powercap, &max_powercap) != 2 || min_powercap < 0 || max_powercap < 0) {
            fprintf(stderr, "[ERROR] Powercap calibration: Invalid format. Use --powercap-calibration <min>-<max>\n");
            return 1;
        }

        if (sscanf(argv[3], "%d", &variance) != 1 || variance < 0) {
            fprintf(stderr, "[ERROR] Powercap calibration: Invalid variance. Must be non-negative.\n");
            return 1;
        }

        if (sscanf(argv[4], "%d", &time_out_limit) != 1 || time_out_limit <= 0) {
            fprintf(stderr, "[ERROR] Powercap calibration: Invalid time_out_limit. Must be positive (minutes).\n");
            return 1;
        }

        time_out_limit = time_out_limit * 60;

        if (sscanf(argv[5], "%d", &secs_to_sleep) != 1 || secs_to_sleep < 0) {
            fprintf(stderr, "[ERROR] Powercap calibration: Invalid secs_to_sleep.\n");
            return 1;
        }

        if (sscanf(argv[6], "%d", &n_times) != 1 || n_times <= 0) {
            fprintf(stderr, "[ERROR] Powercap calibration: Invalid n_times.\n");
            return 1;
        }

        if (argc == 11) {
            strcpy(compile_command, argv[7]);
            strcpy(run_command, argv[8]);
            strcpy(lang, argv[9]);
            strcpy(program_name, argv[10]);
        } else {
            strcpy(run_command, argv[7]);
            strcpy(lang, argv[8]);
            strcpy(program_name, argv[9]);
        }

        if (strlen(compile_command) > 0) {
            printf("[INFO] Powercap calibration: Compiling program with command: %s\n", compile_command);
            if (system(compile_command) != 0) {
                fprintf(stderr, "[ERROR] Powercap calibration: Failed to compile program\n");
                return 1;
            }
        }

        if (access("/tmp/cores_temperatures.txt", F_OK) != 0) {
            printf("[WARNING] Powercap calibration: File /tmp/cores_temperatures.txt not found. Running temperature calibration...\n");
            calibrateTemperature(secs_to_sleep);
        } else {
            printf("[INFO] Powercap calibration: File /tmp/cores_temperatures.txt found. Skipping temperature calibration.\n");
        }
        temperature_threshold = getInitialTemperature();

        calibratePowercap(n_times, core, run_command, lang, program_name, min_powercap, max_powercap, powercap, time_out_limit, temperature_threshold, variance);

        return 0;
    }

    if (argc != 9) {
        fprintf(stderr, "[ERROR] Incorrect number of arguments.\n");
        fprintf(stderr, "Usage: %s <command> <language> <program> <n_times> <variance> <time_out_limit> <sleep_time> <powercap>\n", argv[0]);
        return -1;
    }

    strncpy(command, argv[1], sizeof(command)-1); command[sizeof(command)-1] = '\0';
    strncpy(language, argv[2], sizeof(language)-1); language[sizeof(language)-1] = '\0';
    strncpy(program, argv[3], sizeof(program)-1); program[sizeof(program)-1] = '\0';
    n_times = atoi(argv[4]);
    variance = atoi(argv[5]);
    time_out_limit = atoi(argv[6]); /* hours */
    sleep_time = atoi(argv[7]);
    powercap = atoi(argv[8]);

    if (access("/tmp/cores_temperatures.txt", F_OK) != 0) {
        printf("[WARNING] File /tmp/cores_temperatures.txt not found. Running temperature calibration...\n");
        calibrateTemperature(sleep_time);
    } else {
        printf("[INFO] File /tmp/cores_temperatures.txt found. Skipping temperature calibration.\n");
    }

    temperature_threshold = getInitialTemperature();

    printf("\n[INFO]\n======= Execution Details ========\n");
    printf(" Command    : %s\n", command);
    printf(" Language   : %s\n", language);
    printf(" Program    : %s\n", program);
    printf(" N_times    : %d\n", n_times);
    printf(" Variance   : %d°C\n", variance);
    printf(" Timeout    : %d hours\n", time_out_limit);
    printf(" Sleep_time : %d seconds\n", sleep_time);
    if (powercap == -1) printf(" PowerCap  : No power cap\n"); else printf(" PowerCap  : %d watts\n", powercap);
    printf(" Reference temperature : %.2f °C\n", temperature_threshold);
    printf("====================================\n\n");

    if (sleep_time < 0) { fprintf(stderr, "[ERROR] sleep_time must be >= 0.\n"); return -1; }
    if (n_times <= 0) { fprintf(stderr, "[ERROR] n_times must be > 0.\n"); return -1; }
    if (variance < 0) { fprintf(stderr, "[ERROR] variance must be >= 0.\n"); return -1; }
    if (time_out_limit <= 0) { fprintf(stderr, "[ERROR] time_out_limit must be > 0.\n"); return -1; }
    if (powercap < -1 || powercap == 0) { fprintf(stderr, "[ERROR] powercap must be -1 or > 0.\n"); return -1; }

    time_out_limit = time_out_limit * 3600; /* seconds */

    int memory = measureMemoryUsage(command);

    if (initializeRapl(&rc, powercap) != 0) {
        return -1;
    }

    char sleep_command[100];
    snprintf(sleep_command, sizeof(sleep_command), "sleep %d", sleep_time);

    printf("\n\n BEGINNING of IDLE EXECUTION: \n");
    runTesting(n_times, core, sleep_command, language, program, "measurements.csv", powercap, time_out_limit, temperature_threshold, variance, memory, 1);
    printf("\n\n END of IDLE EXECUTION: \n");

    printf("\n\n BEGINNING of PARAMETRIZED PROGRAM: \n");
    runTesting(n_times, core, command, language, program, "measurements.csv", powercap, time_out_limit, temperature_threshold, variance, memory, 0);
    printf("\n\n END of PARAMETRIZED PROGRAM: \n");

    if (raplcap_destroy(&rc) != 0) {
        perror("[ERROR] raplcap_destroy");
    } else {
        printf("[INFO] Successfully destroyed CAP\n");
    }

    return 0;
}
