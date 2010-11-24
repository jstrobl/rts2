/* 
 * Parser for RA DEC string.
 * Copyright (C) 2008 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <sstream>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "radecparser.h"

double parseDMS (const char *hptr, double *mul)
{
	char *locptr;
	char *endptr;
	double ret;					 //to store return value

	if (!(locptr = strdup (hptr)))
		return nan ("f");

	if (*locptr == '-')
	{
		locptr++;
		*mul = -1;
	}
	else
	{
		*mul = 1;
	}

	endptr = locptr;
	ret = 0;

	while (*endptr)
	{
		errno = 0;
		// convert test
		ret += strtod (locptr, &endptr) * *mul;
		
		if (errno == ERANGE)
			return NAN;
		// we get sucessfuly to end
		if (!*endptr)
		{
			errno = 0;
			return ret;
		}

		// if we have error in translating first string..
		if (locptr == endptr)
		{
			errno = EINVAL;
			return NAN;
		}

		*mul /= 60;
		locptr = endptr + 1;
	}

	errno = 0;
	return ret;
}

int parseRaDec (const char *radec, double &ra, double &dec)
{
	std::istringstream is (radec);
	// now try to parse it to ra dec..
	ra = 0;
	dec = 0;
	int dec_sign = 1;
	int step = 1;
	enum { NOT_GET, RA, DEC } state;
	bool ra_six = false;

	state = NOT_GET;
	while (1)
	{
		double val;
		// try to get number and something after it..
		is >> val;
		if (is.fail ())
			break;

		switch (state)
		{
			case NOT_GET:
				state = RA;
			case RA:
				ra += (val / step);
				if (step > 1)
					ra_six = true;
				step *= 60;
				break;
			case DEC:
				dec += (val / step);
				step *= 60;
				break;
		}

		// find start of next number.
		while (true)
		{
			char next_ch;
			next_ch = is.peek ();
			if (is.fail ())
				break;
			if (isdigit (next_ch) || next_ch == '.')
				break;
			next_ch = is.get ();
			if (is.fail ())
				break;
			if (next_ch == '+' || next_ch == '-')
			{
				if (state != RA)
					return -1;
				state = DEC;
				step = 1;
				if (next_ch == '-')
				{
					dec_sign = -1;
				}
				break;
			}
			else if (!(isspace (next_ch) || next_ch == ':'))
			{
				break;
			}
		}

	}

	dec *= dec_sign;
	if (ra_six)
		ra *= 15;

	if (state == DEC)
		return 0;
	return -1;
}
