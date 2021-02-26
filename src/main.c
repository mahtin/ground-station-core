#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>

#include "ev.h"
#include "sig.h"
#include "sat.h"
#include "log.h"
#include "cfg.h"

void print_help(void)
{
	fprintf(stderr, "ISU GS controller v0.1\n\n"
					" -h, --help                    :  print this help\n"
					" -v, --verbosity <0..3>        :  log verbosity\n"
					" -f, --log-file <filename>     :  redirect logs into a file\n"
					" -r, --remote-addr <ip>        :  remote IP of rotctld daemons\n"
					" -a, --azimuth-port <port>     :  port of the azimuth rotctld\n"
					" -e, --elevation-port <port>   :  port of the elevation rotctld\n"
					" -p, --request-port <port num> :  port number to listen JSON requests\n"
		  			);
}

static struct option long_options[] =
{
	{"request-port",	required_argument,	0, 	'p'},
	{"remote-addr",  	required_argument,	0, 	'r'},
	{"verbosity",		required_argument,	0, 	'v'},
	{"elevation-port",  required_argument,	0, 	'e'},
	{"azimuth-port",  	required_argument,	0, 	'a'},
	{"log-file",   		required_argument,	0, 	'f'},
	{"help",			no_argument,		0, 	'h'},
	{0, 0, 0, 0}
};

int main(int argc, char **argv)
{
	cfg_t *cfg;
	int options;
	int option_index;
	
	cfg = alloc_cfg();
	if (!cfg) {
		fprintf(stderr, "%s(): error during init\n", __func__);
		return -1;
	}

	cfg->log_file = stdout;
	cfg->log_level = LVL_CRIT;
	strncpy(cfg->version, "v0.1-20022021", sizeof(cfg->version));

	do {
		if ((options = getopt_long(argc, argv, "p:r:v:e:a:f:h",
						long_options, &option_index)) == -1) {
			break;
		}

		switch (options) {
			case 'p':
				cfg->listen_port = strtol(optarg, NULL, 10);

				if (cfg->listen_port < 0x400 ||
						cfg->listen_port > 0xFFFF) {
					cfg->listen_port = DEF_LISTEN_PORT;
					fprintf(stderr, "Wrong listen port, set to default: %d\n", DEF_LISTEN_PORT);
				}
				break;
			case 'r':
				strncpy(cfg->cli.remote_ip, optarg, sizeof(cfg->cli.remote_ip));

				if (verify_ip(cfg->cli.remote_ip) == false) {
					fprintf(stderr, "Wrong remote IP address\n");
					goto err;
				}
				break;
			case 'f':
				cfg->log_file = fopen(optarg, "a");
				if (!cfg->log_file) {
					fprintf(stderr, "Couldn't open log file\n");
					goto err;
				}

				fprintf(stdout, "Log file set to %s\n", optarg);
				break;
			case 'v':
				cfg->log_level = strtol(optarg, NULL, 10);

				if (cfg->log_level < LVL_CRIT || cfg->log_level > LVL_VERB) {
					fprintf(stderr, "Wrong log level (%d, valid levels are %d..%d)\n", cfg->log_level, LVL_CRIT, LVL_VERB);
					goto err;
				}
				break;
			case 'a':
				cfg->cli.azimuth_port = strtol(optarg, NULL, 10);
				break;
			case 'e':
				cfg->cli.elevation_port = strtol(optarg, NULL, 10);
				break;
			case 'h':
			default:
				print_help();
				goto err;
		}
	} while (options != -1);

	if (!cfg->cli.azimuth_port ||
		!cfg->cli.elevation_port ||
		!strcmp(cfg->cli.remote_ip, "")) {
		fprintf(stderr, "Please specify remote parameters to connect\n");
		goto err;
	}

	log_init(cfg->log_file, cfg->log_level);

	if (sig_register() == -1)
		return -1;

	if (ev_probe(cfg->listen_port) == -1)
		return -1;

	ev_process_loop();
	return 0;

err:
	destroy_cfg(cfg);
	return -1;
}
