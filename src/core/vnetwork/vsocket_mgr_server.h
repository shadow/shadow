/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VSOCKET_MGR_SERVER_H_
#define VSOCKET_MGR_SERVER_H_

#include "vsocket_mgr.h"
#include "vtcp_server.h"

void vsocket_mgr_add_server(vsocket_mgr_tp net, vtcp_server_tp server);
vtcp_server_tp vsocket_mgr_get_server(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_remove_server(vsocket_mgr_tp net, vtcp_server_tp server);

#endif /* VSOCKET_MGR_SERVER_H_ */
