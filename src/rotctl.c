/*
 * Ground Station Core (GSC)
 * Copyright (C) 2021  International Space University
 * Contributors:
 *   Stanislav Barantsev

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h>
#include <sys/types.h> 
#include <arpa/inet.h>
#include <sys/socket.h> 

#include "log.h"
#include "sat.h"
#include "stats.h"
#include "rotctl.h"

pthread_t az_thread;
pthread_t el_thread;

typedef struct pos_t {
	void *obs;
	double val;
	rot_type_t type;
} pos_t;

static int rotctl_flush_socket(observation_t *obs, rot_type_t type)
{
	int fd;
	int rv;
	int ret;
	fd_set set;
	char *rxbuf = NULL;
	struct timeval timeout;

	ret = 0;

	if (obs == NULL)
		return -1;

	if (type == ROT_TYPE_NONE)
		return -1;

	/** TODO: replace with a relevant define */
	rxbuf = calloc(8192, 1);
	if (!rxbuf)
		return -1;

	fd = (type == ROT_TYPE_AZ) ? obs->cfg->cli.azimuth_conn_fd : obs->cfg->cli.elevation_conn_fd;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	timeout.tv_sec = 0;
	timeout.tv_usec = 10000;

	rv = select(fd + 1, &set, NULL, NULL, &timeout);
	if (rv > 0) {
		recv(fd, rxbuf, 8192, 0);
		printf("Flushing the old data\n");
	}

	LOG_V("rotctl_flush_socket() command done");

	free(rxbuf);
	return ret;
}

static int rotctl_send_recv_or_timeout(observation_t *obs, rot_type_t type, double val)
{
	int fd;
	int rv;
	int ret;
	fd_set set;
	char *rxbuf = NULL;
	char val_buf[64] = { 0 };
	struct timeval timeout;

	ret = 0;

	if (obs == NULL)
		return -1;

	if (type == ROT_TYPE_NONE)
		return -1;

	/** TODO: replace with a relevant define */
	rxbuf = calloc(8192, 1);
	if (!rxbuf)
		return -1;

	fd = (type == ROT_TYPE_AZ) ? obs->cfg->cli.azimuth_conn_fd : obs->cfg->cli.elevation_conn_fd;

	rotctl_flush_socket(obs, type);

	FD_ZERO(&set);
	FD_SET(fd, &set);

	timeout.tv_sec = 120;
	timeout.tv_usec = 0;

	snprintf(val_buf, sizeof(val_buf), "w " "%s" "%.02f\n", (type == ROT_TYPE_AZ) ? "A" : "E", val);
	write(fd, val_buf, strlen(val_buf));

	rv = select(fd + 1, &set, NULL, NULL, &timeout);

	if(rv == -1) {
		ret = -1;
		goto out;
	} else if(rv == 0) {
		LOG_V("select() timeout");
		ret = 0;
		goto out;
	} else {
			/** TODO: replace with a relevant define */
			int received = recv(fd, rxbuf, 8192, 0);
			if (received < 0)
				goto out;
	}

out:
	LOG_V("rotctl_send_recv_or_timeout() command done");

	free(rxbuf);
	return ret;
}

int rotctl_open(observation_t *obs, rot_type_t type)
{
	int ret = 0;
	int *fd = NULL;
	struct sockaddr_in server_addr = { 0 };

	server_addr.sin_family = AF_INET;

	if (type == ROT_TYPE_AZ) {
		fd = &obs->cfg->cli.azimuth_conn_fd;
		server_addr.sin_port = htons(obs->cfg->cli.azimuth_port);
	} else {
		fd = &obs->cfg->cli.elevation_conn_fd;
		server_addr.sin_port = htons(obs->cfg->cli.elevation_port);
	}

	if ((ret = inet_pton(AF_INET, obs->cfg->cli.remote_ip, &server_addr.sin_addr)) != 1) {
		LOG_C("Error on inet_pton");
		return -1;
	}

	if ((*fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		LOG_C("Error on socket");
		return -1;
	}

	if ((ret = connect(*fd, (struct sockaddr*)&server_addr, sizeof(server_addr))) == -1) {
		LOG_C("Error on connect");
		return -1;
	}

	return ret;
}

int rotctl_close(observation_t *obs, rot_type_t type)
{
	int ret;
	int *fd = NULL;

	if (obs == NULL)
		return -1;

	fd = (type == ROT_TYPE_AZ) ? &obs->cfg->cli.azimuth_conn_fd : &obs->cfg->cli.elevation_conn_fd;

	if ((ret = shutdown(*fd, SHUT_RDWR)) == -1) {
		LOG_E("Error on shutdown");
		return -1;
	}

	if ((ret = close(*fd)) == -1) {
		LOG_E("Error on shutdown");
		return -1;
	}

	/** FIXME: replace the return codes to defines */
	return 0;
}

int rotctl_stop(observation_t *obs)
{
	int ret;
	char buf[32];
	char rxbuf[4096];

	ret = 0;

	if (obs == NULL)
		return -1;

	snprintf(buf, sizeof(buf), "w S\n");

	write(obs->cfg->cli.azimuth_conn_fd, buf, strlen(buf));
	read(obs->cfg->cli.azimuth_conn_fd, rxbuf, sizeof(rxbuf));
	/* LOG_I("rxbuf1=%s", rxbuf); */
	write(obs->cfg->cli.elevation_conn_fd, buf, strlen(buf));
	read(obs->cfg->cli.elevation_conn_fd, rxbuf, sizeof(rxbuf));
	/* LOG_I("rxbuf2=%s", rxbuf); */

	LOG_V("rotctl_stop() command done");
	return ret;
}

int rotctl_calibrate(observation_t *obs, bool azimuth, bool elevation)
{
	int ret;
	char buf[32] = { 0 };
	char rxbuf[4096] = { 0 };
	global_stats_t *stats;

	ret = 0;

	stats = stats_get_instance();
	stats->last_azimuth = 0;
	stats->last_elevation = 0;

	if (obs == NULL)
		return -1;

	snprintf(buf, sizeof(buf), "w CAL\n");

	if (azimuth) {
		write(obs->cfg->cli.azimuth_conn_fd, buf, strlen(buf));
		read(obs->cfg->cli.azimuth_conn_fd, rxbuf, sizeof(rxbuf));
	}

	if (elevation) {
		write(obs->cfg->cli.elevation_conn_fd, buf, strlen(buf));
		read(obs->cfg->cli.elevation_conn_fd, rxbuf, sizeof(rxbuf));
	}

	sleep(120);

	LOG_V("rotctl_calibrate() command done");
	return ret;
}

float rotctl_extract_value(char *string)
{
	char *substr = strstr(string, "=");

	if (!substr)
		return 0;

	substr++;

	char *endstr = strchr(substr, ' ');
	if (!endstr)
		return 0;

	*endstr = 0;

	return strtof(substr, NULL);
}

float rotctl_get_azimuth(observation_t *obs)
{
	char az_buf[32] = { 0 };
	char rxbuf[4096] = { 0 };

	if (obs == NULL)
		return -1;

	snprintf(az_buf, sizeof(az_buf), "w A\n");

	write(obs->cfg->cli.azimuth_conn_fd, az_buf, strlen(az_buf));
	read(obs->cfg->cli.azimuth_conn_fd, rxbuf, sizeof(rxbuf));

	return rotctl_extract_value(rxbuf);
}

float rotctl_get_elevation(observation_t *obs)
{
	char el_buf[32] = { 0 };
	char rxbuf[4096] = { 0 };

	if (obs == NULL)
		return -1;

	snprintf(el_buf, sizeof(el_buf), "w E\n");

	write(obs->cfg->cli.elevation_conn_fd, el_buf, strlen(el_buf));
	read(obs->cfg->cli.elevation_conn_fd, rxbuf, sizeof(rxbuf));

	return rotctl_extract_value(rxbuf);
}

static void *rotctl_park(void *opt)
{
	pos_t *pos = (pos_t *) opt;

	rotctl_send_recv_or_timeout(pos->obs, pos->type, pos->val);
	return NULL;
}

int rotctl_park_and_wait(observation_t *obs, double az, double el)
{
	int ret;
	global_stats_t *stats;

	pos_t pos_az = {obs, az, ROT_TYPE_AZ};
	pos_t pos_el = {obs, el, ROT_TYPE_EL};

	stats = stats_get_instance();
	stats->last_azimuth = az;
	stats->last_elevation = el;

	ret = 0;

	if (obs == NULL)
		return -1;

	pthread_create(&az_thread, NULL, rotctl_park, &pos_az);
	pthread_create(&el_thread, NULL, rotctl_park, &pos_el);

	pthread_join(az_thread, NULL);
	pthread_join(el_thread, NULL);

	LOG_V("rotctl_send_and_wait() command done");
	return ret;
}

int rotctl_send_az(observation_t *obs, double az)
{
	global_stats_t *stats;

	stats = stats_get_instance();
	stats->last_azimuth = az;

	return rotctl_send_recv_or_timeout(obs, ROT_TYPE_AZ, az);
}

int rotctl_send_el(observation_t *obs, double el)
{
	global_stats_t *stats;

	stats = stats_get_instance();
	stats->last_elevation = el;

	return rotctl_send_recv_or_timeout(obs, ROT_TYPE_EL, el);
}

