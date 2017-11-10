/*
 * gpio-isr.c:
 *
 * Copyright (c) 2017 Peter Lieven
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h> 
#include <unistd.h>
#include <sys/stat.h>
#include <pigpio.h>

#define MAX_PINS 54
#define VOLATILE_DIR "/run/gpio-isr"
#define STATIC_DIR "/var/lib/gpio-isr"

static volatile uint64_t g_totalCount[MAX_PINS];
static volatile unsigned g_lastPeriod[MAX_PINS], g_ignoreCount[MAX_PINS], g_lastInterrupt[MAX_PINS], g_lastRise[MAX_PINS];
static volatile uint8_t g_inverseLogic[MAX_PINS], g_expectedPeriod[MAX_PINS], g_lastLevel[MAX_PINS];
static volatile unsigned g_pendingInt;

static unsigned g_monitor, g_debug, g_exit, g_dumpint = 300000000;
static char *g_arg0;

void myInterrupt(int pin, int level, uint32_t tick) {
	if (g_inverseLogic[pin]) level = 1 - level;
	if (level) {
		g_lastRise[pin] = tick;
		g_lastLevel[pin] = level; 
		return;
	}
	unsigned diff = tick - g_lastInterrupt[pin];
	unsigned pulseWidth = tick - g_lastRise[pin];
	if (!g_lastLevel[pin]) {
		fprintf(stderr, "Ignoring INT on GPIO%d no previous rising edge\n", pin);
		g_ignoreCount[pin]++;
		return;
	}
	g_lastLevel[pin] = level;
	if (!g_expectedPeriod[pin] && (pulseWidth < 30000 || pulseWidth > 120000)) {
		fprintf(stderr, "Ignoring INT on GPIO%d with pulseWidth %ums\n", pin, pulseWidth / 1000);
		g_ignoreCount[pin]++;
		return;
	}
	if (g_expectedPeriod[pin] && (pulseWidth < (g_expectedPeriod[pin] * 1000 - 2000) || pulseWidth > (g_expectedPeriod[pin] * 1000 + 2000))) {
		fprintf(stderr, "Ignoring INT on GPIO%d with pulseWidth %ums (more than 2ms from expectedPeriod %ums)\n", pin, pulseWidth / 1000, g_expectedPeriod[pin]);
		g_ignoreCount[pin]++;
		return;
	}
	g_totalCount[pin]++;
	g_lastPeriod[pin] = diff / 1000;
	g_lastInterrupt[pin] = tick;
	if (g_ignoreCount[pin]) {
		g_ignoreCount[pin]--;
	}
	g_pendingInt++;
	if (g_debug) {
		fprintf(stderr, "INT on GPIO%u pulseWidth %ums totalCount %" PRIu64 " lastPeriod %ums\n", pin, pulseWidth / 1000, g_totalCount[pin], g_lastPeriod[pin]);
	}
	//XXX: handle timeout and set period to INF
}

void usage(void) {
	fprintf(stderr, "Usage: %s [-d] [-m] [-t dumpIntSecs] [-w <pulseWidth] [-U] [-D] -p|P pinA [-p|P pinB] ... [-p|P pinX]\n", g_arg0);
	fprintf(stderr, "\n         -t  dump counters and periods each dumpIntSecs to non-volatile files (default 300s)");
	fprintf(stderr, "\n         -m  monitor mode, do not read or write any files (implies -d)");
	fprintf(stderr, "\n         -p  pulse consinsts of rising and falling edge (normal low)");
	fprintf(stderr, "\n         -P  pulse consinsts of falling and rising edge (normal high)");
	fprintf(stderr, "\n         -d  debug mode, log every interrupt\n");
	fprintf(stderr, "\n The following switches apply to all pins if specified before the first pin. If specified");
	fprintf(stderr, "\n after a pin statement they apply only to the last pin:");
	fprintf(stderr, "\n         -w  expected pulseWidth (ignores everything more than +/- 2ms difference)");
	fprintf(stderr, "\n         -U  enable internal pull-up");
	fprintf(stderr, "\n         -D  enable internal pull-down\n");
	exit(1);
}

void signal_handler(int signal) {
	g_exit = 1;
}

void ignore_signal(int signal) {
}

int main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	unsigned lastdump, lastdump2;
	unsigned intEnabled[MAX_PINS] = {}, pullUp[MAX_PINS] = {}, pullDown[MAX_PINS] = {};
	int i, c, v, pinCnt = 0, lastPin = -1;
	g_arg0 = argv[0];
	char buf[PATH_MAX];

	while ((c = getopt(argc, argv, "dmw:UDt:p:P:")) != -1) {
		switch (c) {
		case 'd':
			g_debug = 1;
			break;
		case 'm':
			g_monitor = 1;
			g_debug = 1;
			break;
		case 't':
			g_dumpint = atoi(optarg) * 1000000;
			break;
		case 'w':
			v = atoi(optarg);
			if (v < 1 || v > 120) usage();
			if (lastPin == -1) {
				for (i = 0; i < MAX_PINS; i++) g_expectedPeriod[i] = v ;
				fprintf(stderr, "setting expected pulseWidth for all GPIO pins to %d ms\n", v);
			} else {
				g_expectedPeriod[lastPin] = v;
				fprintf(stderr, "setting expected pulseWidth for GPIO%d to %d ms\n", lastPin, v);
			}
			break;
		case 'U':
			if (lastPin == -1) {
				for (i = 0; i < MAX_PINS; i++) pullUp[i] = 1;
			} else {
				pullUp[lastPin] = 1;
			}
			break;
		case 'D':
			if (lastPin == -1) {
				for (i = 0; i < MAX_PINS; i++) pullDown[i] = 1;
			} else {
				pullDown[lastPin] = 1;
			}
			break;
		case 'P':
		case 'p': {
			int pin = atoi(optarg);
			if (pin < 0 || pin >= MAX_PINS) usage();
			intEnabled[pin]++;
			pinCnt++;
			if (c == 'P') g_inverseLogic[pin] = 1;
			lastPin = pin;
			break;
		}
		default:
			usage();
		}
	}

	if (!pinCnt || optind != argc) usage();

	if (g_monitor) {
		fprintf(stderr, "MONITOR MODE, will not read or write any files\n");
	} else {
		fprintf(stderr, "will read/write static counter information to %s\n", STATIC_DIR);
		if (mkdir(STATIC_DIR, 0755) && errno != EEXIST) {
			fprintf(stderr, "failed to create directory: %s\n", strerror(errno));
			exit(1);
		}
		fprintf(stderr, "will read/write volatile counter information to %s\n", VOLATILE_DIR);
		if (mkdir(VOLATILE_DIR, 0755) && errno != EEXIST) {
			fprintf(stderr, "failed to create directory: %s\n", strerror(errno));
			exit(1);
		}
	}

	if (gpioInitialise() == PI_INIT_FAILED) {
		fprintf(stderr, "gpioInitialise() failed\n");
		exit(1);
	}

	gpioSetSignalFunc(SIGTERM, &signal_handler);
	gpioSetSignalFunc(SIGINT, &signal_handler);
	gpioSetSignalFunc(SIGCONT, &ignore_signal);

	lastdump = lastdump2 = gpioTick();

	for (i = 0; i < MAX_PINS; i++) {
		FILE *file;
		if (!intEnabled[i]) continue;
		g_lastInterrupt[i] = gpioTick();
		fprintf(stderr, "setting GPIO%d for direction IN -> ", i);
		if (gpioSetMode(i, PI_INPUT)) fprintf(stderr, "FAILED\n"); else fprintf(stderr, "OK\n");
		if (pullUp[i]) {
			fprintf(stderr, "enabled internal pull-up for GPIO%d -> ", i);
			if (gpioSetPullUpDown(i, PI_PUD_UP)) fprintf(stderr, "FAILED\n"); else fprintf(stderr, "OK\n"); 
		} else if (pullDown[i]) {
			fprintf(stderr, "enabled internal pull-down for GPIO%d -> ", i);
			if (gpioSetPullUpDown(i, PI_PUD_DOWN)) fprintf(stderr, "FAILED\n"); else fprintf(stderr, "OK\n"); 
		}
		g_ignoreCount[i] = 2; /* ignore first pulse because we haven't seen the last pulse */
		fprintf(stderr, "setting up interrupt handler for GPIO%d -> ", i);
		if (gpioSetISRFunc(i, EITHER_EDGE, 0, &myInterrupt)) fprintf(stderr, "FAILED\n"); else fprintf(stderr, "OK\n");
		if (!g_monitor) {
			snprintf(buf, PATH_MAX, "%s/pin%d.totalCount", STATIC_DIR, i);
			file = fopen(buf, "r");
			if (file) {
				if (fscanf (file, "%"PRIu64, &g_totalCount[i]) == 1) {
					fprintf(stderr, "initialized totalCount for GPIO%d to %" PRIu64 " from %s\n", i, g_totalCount[i], buf);
				}
				fclose(file);
			}
		}
	}

	while (1) {
		FILE *file;
		unsigned now = gpioTick();
		unsigned diff = now - lastdump;
		if (!g_monitor && (g_exit || diff >= g_dumpint)) {
			for (i = 0; i <  MAX_PINS; i++) {
				if (!intEnabled[i]) continue;
				snprintf(buf, PATH_MAX, "%s/pin%d.totalCount", STATIC_DIR, i);
				file = fopen(buf, "w");
				if (file) {
					fprintf(file, "%" PRIu64 "\n", g_totalCount[i]);
					fprintf(stderr, "updated %s for GPIO%d to %" PRIu64 "\n", buf, i, g_totalCount[i]);
					fclose(file);
				} else {
					fprintf(stderr, "cannot open %s for writing: %s", buf, strerror(errno));
					exit(1);
				}
			}
			lastdump = now;
		}
		if (g_exit) break;
		while (!g_monitor && g_pendingInt > 0) {
			g_pendingInt--;
			for (i = 0; i <  MAX_PINS; i++) {
				unsigned diff2 = g_lastInterrupt[i] - lastdump2;
				if (!intEnabled[i]) continue;
				if (diff2 < 0x7fffffff) {
					snprintf(buf, PATH_MAX, "%s/pin%d.totalCount", VOLATILE_DIR, i);
					file = fopen(buf, "w");
					if (file) {
						fprintf(file, "%" PRIu64 "\n", g_totalCount[i]);
						if (g_debug) fprintf(stderr, "updated %s for GPIO%d to %" PRIu64 "\n", buf, i, g_totalCount[i]);
						fclose(file);
					}
					if (lastdump2 > 0 && !g_ignoreCount[i]) {
						snprintf(buf, PATH_MAX, "%s/pin%d.lastPeriod", VOLATILE_DIR, i);
						file = fopen(buf, "w");
						if (file) {
							fprintf(file, "%u\n", g_lastPeriod[i]);
							if (g_debug) fprintf(stderr, "updated %s for GPIO%d to %u\n", buf, i, g_lastPeriod[i]);
							fclose(file);
						}
					}
					lastdump2 = now;
				}
			}
		}
		gpioDelay(20000);
	}

	gpioTerminate();
	exit(0);
}
