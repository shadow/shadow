/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

/*
 * gcc -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c
 * cp testplugin.so /tmp/testplugin1.so
 * cp testplugin.so /tmp/testplugin2.so
 */

#include <stdio.h>

int test = 0;

__init__() {
	test++;
	printf("%i after increment\n", test);
}
