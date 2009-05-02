/**
 * Copyright (C) 2005-2007 Petr Kubanek <petr@kubanek.net>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "focusd.h"

#include "libfli.h"

namespace rts2focusd
{

/**
 * FLI focuser driver. You will need FLIlib and option to ./configure
 * (--with-fli=<llibflidir>) to get that running. Please read ../../INSTALL.fli
 * for instructions.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class Fli:public Focusd
{
	private:
		flidev_t dev;
		flidomain_t deviceDomain;

		int fliDebug;

	protected:
		virtual int isFocusing ();
		virtual bool isAtStartPosition ();

		virtual int processOption (int in_opt);
		virtual int init ();
		virtual int initValues ();
		virtual int info ();
		virtual int setTo (int num);
		virtual int home ();

	public:
		Fli (int argc, char **argv);
		virtual ~ Fli (void);
};

};

using namespace rts2focusd;

Fli::Fli (int argc, char **argv)
:Focusd (argc, argv)
{
	deviceDomain = FLIDEVICE_FOCUSER | FLIDOMAIN_USB;
	fliDebug = FLIDEBUG_NONE;
	addOption ('D', "domain", 1,
		"CCD Domain (default to USB; possible values: USB|LPT|SERIAL|INET)");
	addOption ('b', "fli_debug", 1,
		"FLI debug level (1, 2 or 3; 3 will print most error message to stdout)");
}


Fli::~Fli (void)
{
	FLIClose (dev);
}


int
Fli::processOption (int in_opt)
{
	switch (in_opt)
	{
		case 'D':
			deviceDomain = FLIDEVICE_FOCUSER;
			if (!strcasecmp ("USB", optarg))
				deviceDomain |= FLIDOMAIN_USB;
			else if (!strcasecmp ("LPT", optarg))
				deviceDomain |= FLIDOMAIN_PARALLEL_PORT;
			else if (!strcasecmp ("SERIAL", optarg))
				deviceDomain |= FLIDOMAIN_SERIAL;
			else if (!strcasecmp ("INET", optarg))
				deviceDomain |= FLIDOMAIN_INET;
			else if (!strcasecmp ("SERIAL_19200", optarg))
				deviceDomain |= FLIDOMAIN_SERIAL_19200;
			else if (!strcasecmp ("SERIAL_1200", optarg))
				deviceDomain |= FLIDOMAIN_SERIAL_1200;
			else
				return -1;
			break;
		case 'b':
			switch (atoi (optarg))
			{
				case 1:
					fliDebug = FLIDEBUG_FAIL;
					break;
				case 2:
					fliDebug = FLIDEBUG_FAIL | FLIDEBUG_WARN;
					break;
				case 3:
					fliDebug = FLIDEBUG_ALL;
					break;
			}
			break;
		default:
			return Focusd::processOption (in_opt);
	}
	return 0;
}


int
Fli::init ()
{
	LIBFLIAPI ret;
	int ret_f;
	char **names;
	char *nam_sep;

	ret_f = Focusd::init ();
	if (ret_f)
		return ret_f;

	if (fliDebug)
		FLISetDebugLevel (NULL, FLIDEBUG_ALL);

	ret = FLIList (deviceDomain, &names);
	if (ret)
		return -1;

	if (names[0] == NULL)
	{
		logStream (MESSAGE_ERROR) << "Fli::init No device found!"
			<< sendLog;
		return -1;
	}

	nam_sep = strchr (names[0], ';');
	if (nam_sep)
		*nam_sep = '\0';

	ret = FLIOpen (&dev, names[0], deviceDomain);
	FLIFreeList (names);
	if (ret)
		return -1;

	return 0;
}


int
Fli::initValues ()
{
	LIBFLIAPI ret;

	char ft[200];

	ret = FLIGetModel (dev, ft, 200);
	if (ret)
	{
		logStream (MESSAGE_ERROR) << "Cannot get focuser model, error: " << ret << sendLog;
		return -1;
	}
	focType = std::string (ft);
	return Focusd::initValues ();
}


int
Fli::info ()
{
	LIBFLIAPI ret;

	long steps;

	ret = FLIGetStepperPosition (dev, &steps);
	if (ret)
		return -1;

	position->setValueInteger ((int) steps);

	return Focusd::info ();
}


int
Fli::setTo (int num)
{
	LIBFLIAPI ret;
	ret = info ();
	if (ret)
		return ret;

	num -= position->getValueInteger ();

	ret = FLIStepMotorAsync (dev, (long) num);
	if (ret)
		return -1;
	return 0;
}


int
Fli::home ()
{
	LIBFLIAPI ret;
	ret = FLIHomeFocuser (dev);
	if (ret)
		return -1;
	return Focusd::home ();
}


int
Fli::isFocusing ()
{
	LIBFLIAPI ret;
	long rem;

	ret = FLIGetStepsRemaining (dev, &rem);
	if (ret)
		return -1;
	if (rem == 0)
		return -2;
	return USEC_SEC / 100;
}


bool
Fli::isAtStartPosition ()
{
	int ret;
	ret = info ();
	if (ret)
		return false;
	return getPosition () == 0;
}


int
main (int argc, char **argv)
{
	Fli device = Fli (argc, argv);
	return device.run ();
}
