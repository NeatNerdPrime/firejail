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
#ifndef FNET_H
#define FNET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/common.h"

// veth.c
int net_create_veth(const char *dev, const char *nsdev, unsigned pid);
int net_create_macvlan(const char *dev, const char *parent, unsigned pid);
int net_move_interface(const char *dev, unsigned pid);

// interface.c
void net_bridge_add_interface(const char *bridge, const char *dev);
void net_if_up(const char *ifname);
int net_get_mtu(const char *ifname);
void net_set_mtu(const char *ifname, int mtu);

#endif
