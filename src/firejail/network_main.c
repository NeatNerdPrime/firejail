/*
 * Copyright (C) 2014-2016 Firejail Authors
 *
 * This file is part of firejail project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "firejail.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <net/if.h>
#include <stdarg.h>

// configure bridge structure
// - extract ip address and mask from the bridge interface
void net_configure_bridge(Bridge *br, char *dev_name) {
	assert(br);
	assert(dev_name);

	br->dev = dev_name;

	// check the bridge device exists
	char sysbridge[30 + strlen(br->dev)];
	sprintf(sysbridge, "/sys/class/net/%s/bridge", br->dev);
	struct stat s;
	int rv = stat(sysbridge, &s);
	if (rv == 0) {
		// this is a bridge device
		br->macvlan = 0;
	}
	else {
		// is this a regular Ethernet interface
		if (if_nametoindex(br->dev) > 0) {
			br->macvlan = 1;
			char *newname;
			if (asprintf(&newname, "%s-%u", br->devsandbox, getpid()) == -1)
				errExit("asprintf");
			br->devsandbox = newname;
		}			
		else {
			fprintf(stderr, "Error: cannot find network device %s\n", br->dev);
			exit(1);
		}
	}

	// allow unconfigured interfaces
	if (net_get_if_addr(br->dev, &br->ip, &br->mask, br->mac, &br->mtu)) {
		fprintf(stderr, "Warning: the network interface %s is not configured\n", br->dev);
		br->configured = 1;
		br->arg_ip_none = 1;
		return;
	}
	if (arg_debug) {
		if (br->macvlan == 0)
			printf("Bridge device %s at %d.%d.%d.%d/%d\n",
				br->dev, PRINT_IP(br->ip), mask2bits(br->mask));
		else
			printf("macvlan parent device %s at %d.%d.%d.%d/%d\n",
				br->dev, PRINT_IP(br->ip), mask2bits(br->mask));
	}
	
	uint32_t range = ~br->mask + 1;		  // the number of potential addresses
	// this software is not supported for /31 networks
	if (range < 4) {
		fprintf(stderr, "Error: the software is not supported for /31 networks\n");
		exit(1);
	}
	br->configured = 1;
}


void net_configure_sandbox_ip(Bridge *br) {
	assert(br);
	if (br->configured == 0)
		return;

	if (br->arg_ip_none)
		br->ipsandbox = 0;
	else if (br->ipsandbox) {
		// check network range
		char *rv = in_netrange(br->ipsandbox, br->ip, br->mask);
		if (rv) {
			fprintf(stderr, "%s", rv);
			exit(1);
		}
		// send an ARP request and check if there is anybody on this IP address
		if (arp_check(br->dev, br->ipsandbox, br->ip)) {
			fprintf(stderr, "Error: IP address %d.%d.%d.%d is already in use\n", PRINT_IP(br->ipsandbox));
			exit(1);
		}
	}
	else
		// ip address assigned by arp-scan for a bridge device
		br->ipsandbox = arp_assign(br->dev, br); //br->ip, br->mask);
}


// create a veth pair
// - br - bridge device
// - ifname - interface name in sandbox namespace
// - child - child process running the namespace

void net_configure_veth_pair(Bridge *br, const char *ifname, pid_t child) {
	assert(br);
	if (br->configured == 0)
		return;

	// create a veth pair
	char *dev;
	if (br->veth_name == NULL) {
		if (asprintf(&dev, "veth%u%s", getpid(), ifname) < 0)
			errExit("asprintf");
	}
	else
		dev = br->veth_name;
		
//	net_create_veth(dev, ifname, child);
	char *cstr;
	if (asprintf(&cstr, "%d", child) == -1)
		errExit("asprintf");
	sbox_run(SBOX_ROOT | SBOX_SECCOMP, 7, PATH_FNET, "create", "veth", dev, ifname, br->dev, cstr);
	free(cstr);

	char *msg;
	if (asprintf(&msg, "%d.%d.%d.%d address assigned to sandbox", PRINT_IP(br->ipsandbox)) == -1)
		errExit("asprintf");
	logmsg(msg);
	fflush(0);
	free(msg);
}

// the default address should be in the range of at least on of the bridge devices
void check_default_gw(uint32_t defaultgw) {
	assert(defaultgw);

	if (cfg.bridge0.configured) {
		char *rv = in_netrange(defaultgw, cfg.bridge0.ip, cfg.bridge0.mask);
		if (rv == 0)
			return;
	}
	if (cfg.bridge1.configured) {
		char *rv = in_netrange(defaultgw, cfg.bridge1.ip, cfg.bridge1.mask);
		if (rv == 0)
			return;
	}
	if (cfg.bridge2.configured) {
		char *rv = in_netrange(defaultgw, cfg.bridge2.ip, cfg.bridge2.mask);
		if (rv == 0)
			return;
	}
	if (cfg.bridge3.configured) {
		char *rv = in_netrange(defaultgw, cfg.bridge3.ip, cfg.bridge3.mask);
		if (rv == 0)
			return;
	}

	fprintf(stderr, "Error: default gateway %d.%d.%d.%d is not in the range of any network\n", PRINT_IP(defaultgw));
	exit(1);
}

void net_check_cfg(void) {
	EUID_ASSERT();
	int net_configured = 0;
	if (cfg.bridge0.configured)
		net_configured++;
	if (cfg.bridge1.configured)
		net_configured++;
	if (cfg.bridge2.configured)
		net_configured++;
	if (cfg.bridge3.configured)
		net_configured++;

	int if_configured = 0;
	if (cfg.interface0.configured)
		if_configured++;
	if (cfg.interface1.configured)
		if_configured++;
	if (cfg.interface2.configured)
		if_configured++;
	if (cfg.interface3.configured)
		if_configured++;

	// --defaultgw requires a network or an interface
	if (cfg.defaultgw && net_configured == 0 && if_configured == 0) {
		fprintf(stderr, "Error: option --defaultgw requires at least one network or one interface to be configured\n");
		exit(1);
	}

	if (net_configured == 0) // nothing to check
		return;

	// --net=none
	if (arg_nonetwork && net_configured) {
		fprintf(stderr, "Error: --net and --net=none are mutually exclusive\n");
		exit(1);
	}

	// check default gateway address or assign one
	assert(cfg.bridge0.configured);
	if (cfg.defaultgw)
		check_default_gw(cfg.defaultgw);
	else {
		// first network is a regular bridge
		if (cfg.bridge0.macvlan == 0)
			cfg.defaultgw = cfg.bridge0.ip;
		// first network is a mac device
		else {
			// get the host default gw
			EUID_ROOT();	// rise permissions for grsecurity
			// Error fopen:network_get_defaultgw(479): Permission denied
			uint32_t gw = network_get_defaultgw();
			EUID_USER();
			// check the gateway is network range
			if (in_netrange(gw, cfg.bridge0.ip, cfg.bridge0.mask))
				gw = 0;
			cfg.defaultgw = gw;
		}
	}
}



void net_dns_print_name(const char *name) {
	EUID_ASSERT();
	if (!name || strlen(name) == 0) {
		fprintf(stderr, "Error: invalid sandbox name\n");
		exit(1);
	}
	pid_t pid;
	if (name2pid(name, &pid)) {
		fprintf(stderr, "Error: cannot find sandbox %s\n", name);
		exit(1);
	}

	net_dns_print(pid);
}

#define MAXBUF 4096
void net_dns_print(pid_t pid) {
	EUID_ASSERT();
	// drop privileges - will not be able to read /etc/resolv.conf for --noroot option

	// if the pid is that of a firejail  process, use the pid of the first child process
	EUID_ROOT();
	char *comm = pid_proc_comm(pid);
	EUID_USER();
	if (comm) {
		if (strcmp(comm, "firejail") == 0) {
			pid_t child;
			if (find_child(pid, &child) == 0) {
				pid = child;
			}
		}
		free(comm);
	}
	
	char *fname;
	EUID_ROOT();
	if (asprintf(&fname, "/proc/%d/root/etc/resolv.conf", pid) == -1)
		errExit("asprintf");

	// access /etc/resolv.conf
	FILE *fp = fopen(fname, "r");
	if (!fp) {
		fprintf(stderr, "Error: cannot access /etc/resolv.conf\n");
		exit(1);
	}
	
	char buf[MAXBUF];
	while (fgets(buf, MAXBUF, fp))
		printf("%s", buf);
	printf("\n");
	fclose(fp);
	free(fname);
	exit(0);
}

void network_main(pid_t child) {
	char *cstr;
	if (asprintf(&cstr, "%d", child) == -1)
		errExit("asprintf");

	// create veth pair or macvlan device
	if (cfg.bridge0.configured) {
		if (cfg.bridge0.macvlan == 0) {
			net_configure_veth_pair(&cfg.bridge0, "eth0", child);
		}
		else
//			net_create_macvlan(cfg.bridge0.devsandbox, cfg.bridge0.dev, child);
			sbox_run(SBOX_ROOT | SBOX_SECCOMP, 6, PATH_FNET, "create", "macvlan", cfg.bridge0.devsandbox, cfg.bridge0.dev, cstr);
	}
	
	if (cfg.bridge1.configured) {
		if (cfg.bridge1.macvlan == 0)
			net_configure_veth_pair(&cfg.bridge1, "eth1", child);
		else
//			net_create_macvlan(cfg.bridge1.devsandbox, cfg.bridge1.dev, child);
			sbox_run(SBOX_ROOT | SBOX_SECCOMP, 6, PATH_FNET, "create", "macvlan", cfg.bridge1.devsandbox, cfg.bridge1.dev, cstr);
	}
	
	if (cfg.bridge2.configured) {
		if (cfg.bridge2.macvlan == 0)
			net_configure_veth_pair(&cfg.bridge2, "eth2", child);
		else
//			net_create_macvlan(cfg.bridge2.devsandbox, cfg.bridge2.dev, child);
			sbox_run(SBOX_ROOT | SBOX_SECCOMP, 6, PATH_FNET, "create", "macvlan", cfg.bridge2.devsandbox, cfg.bridge2.dev, cstr);
	}
	
	if (cfg.bridge3.configured) {
		if (cfg.bridge3.macvlan == 0)
			net_configure_veth_pair(&cfg.bridge3, "eth3", child);
		else
//			net_create_macvlan(cfg.bridge3.devsandbox, cfg.bridge3.dev, child);
			sbox_run(SBOX_ROOT | SBOX_SECCOMP, 6, PATH_FNET, "create", "macvlan", cfg.bridge3.devsandbox, cfg.bridge3.dev, cstr);
	}

	// move interfaces in sandbox
	if (cfg.interface0.configured) {
//		net_move_interface(cfg.interface0.dev, child);
		sbox_run(SBOX_ROOT | SBOX_SECCOMP, 4, PATH_FNET, "moveif", cfg.interface0.dev, cstr);
	}
	if (cfg.interface1.configured) {
//		net_move_interface(cfg.interface1.dev, child);
		sbox_run(SBOX_ROOT | SBOX_SECCOMP, 4, PATH_FNET, "moveif", cfg.interface1.dev, cstr);
	}
	if (cfg.interface2.configured) {
//		net_move_interface(cfg.interface2.dev, child);
		sbox_run(SBOX_ROOT | SBOX_SECCOMP, 4, PATH_FNET, "moveif", cfg.interface3.dev, cstr);
	}
	if (cfg.interface3.configured) {
//		net_move_interface(cfg.interface3.dev, child);
		sbox_run(SBOX_ROOT | SBOX_SECCOMP, 4, PATH_FNET, "moveif", cfg.interface3.dev, cstr);
	}

	free(cstr);
}
