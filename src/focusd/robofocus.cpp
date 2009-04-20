/**
 * Copyright (C) 2005-2008 Petr Kubanek <petr@kubanek.net>
 * Copyright (C) 2005-2006 John French
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

#define BAUDRATE B9600
#define FOCUSER_PORT "/dev/ttyS1"

#define CMD_FOCUS_MOVE_IN     "FI"
#define CMD_FOCUS_MOVE_OUT    "FO"
#define CMD_FOCUS_GET         "FG"
#define CMD_TEMP_GET          "FT"
#define CMD_FOCUS_GOTO        "FG"

#include "focusd.h"
#include "../utils/rts2connserial.h"

namespace rts2focusd
{

class Robofocus:public Focusd
{
	private:
		const char *device_file;
		Rts2ConnSerial *robofocConn;
		char checksum;
		int step_num;

								 // bitfield holding power switches state - for Robofocus
		Rts2ValueInteger *focSwitches;
		int switchNum;

		// high-level I/O functions
		int focus_move (const char *cmd, int steps);
		void compute_checksum (char *cmd);

		int getPos (Rts2ValueInteger * position);
		int getTemp (Rts2ValueFloat * temperature);
		int getSwitchState ();
	protected:
		virtual int processOption (int in_opt);
		virtual int isFocusing ();

		virtual bool isAtStartPosition ()
		{
			return false;
		}
	public:
		Robofocus (int argc, char **argv);
		~Robofocus (void);

		virtual int init ();
		virtual int initValues ();
		virtual int info ();
		virtual int stepOut (int num);
		virtual int setTo (int num);
		virtual int setSwitch (int switch_num, int new_state);
};

};

using namespace rts2focusd;

Robofocus::Robofocus (int argc, char **argv):Focusd (argc, argv)
{
	device_file = FOCUSER_PORT;

	createValue (focSwitches, "switches", "focuser switches", false);
	switchNum = 4;

	createFocTemp ();

	addOption ('f', "device_file", 1, "device file (ussualy /dev/ttySx");

}


Robofocus::~Robofocus ()
{
  	delete robofocConn;
}


int
Robofocus::processOption (int in_opt)
{
	switch (in_opt)
	{
		case 'f':
			device_file = optarg;
			break;
		default:
			return Focusd::processOption (in_opt);
	}
	return 0;
}


/*!
 * Init focuser, connect on given port port, set manual regime
 *
 * @return 0 on succes, -1 & set errno otherwise
 */
int
Robofocus::init ()
{
	int ret;

	ret = Focusd::init ();

	if (ret)
		return ret;

	robofocConn = new Rts2ConnSerial (device_file, this, BS9600, C8, NONE, 40);
	ret = robofocConn->init ();
	if (ret)
		return ret;

	robofocConn->flushPortIO ();

	// turn paramount on
	ret = setSwitch (1, 1);
	if (ret)
		return -1;

	ret = setSwitch (2, 1);
	if (ret)
	  	return -1;

	return 0;
}


int
Robofocus::initValues ()
{
	focType = std::string ("ROBOFOCUS");
	addConstValue ("switch_num", switchNum);
	return Focusd::initValues ();
}


int
Robofocus::info ()
{
	getPos (focPos);
	getTemp (focTemp);
	// querry for switch state
	int swstate = getSwitchState ();
	if (swstate >= 0)
		focSwitches->setValueInteger (swstate);
	return Focusd::info ();
}


int
Robofocus::getPos (Rts2ValueInteger * position)
{
	char command[10], rbuf[10];
	char command_buffer[9];

	// Put together command with neccessary checksum
	sprintf (command_buffer, "FG000000");
	compute_checksum (command_buffer);

	sprintf (command, "%s%c", command_buffer, checksum);

	if (robofocConn->writeRead (command, 9, rbuf, 9) != 9)
		return -1;
	position->setValueInteger (atoi (rbuf + 2));
	return 0;
}


int
Robofocus::getTemp (Rts2ValueFloat * temp)
{
	char command[10], rbuf[10];
	char command_buffer[9];

	// Put together command with neccessary checksum
	sprintf (command_buffer, "FT000000");
	compute_checksum (command_buffer);

	sprintf (command, "%s%c", command_buffer, checksum);

	if (robofocConn->writeRead (command, 9, rbuf, 9) != 9)
		return -1;
								 // return temp in Celsius
	temp->setValueFloat ((atof (rbuf + 2) / 2) - 273.15);
	return 0;
}


int
Robofocus::getSwitchState ()
{
	char command[10], rbuf[10];
	char command_buffer[9];
	int ret;

	sprintf (command_buffer, "FP000000");
	compute_checksum (command_buffer);

	sprintf (command, "%s%c", command_buffer, checksum);
	if (robofocConn->writeRead (command, 9, rbuf, 9) != 9)
		return -1;
	ret = 0;
	for (int i = 0; i < switchNum; i++)
	{
		if (rbuf[i + 4] == '2')
			ret |= (1 << i);
	}
	return ret;
}


int
Robofocus::stepOut (int num)
{
	if (num < 0)
		return focus_move (CMD_FOCUS_MOVE_IN, -1 * num);
	return focus_move (CMD_FOCUS_MOVE_OUT, num);
}


int
Robofocus::setTo (int num)
{
	char command[9], command_buf[10];
	sprintf (command, "FG%06i", num);
	compute_checksum (command);
	sprintf (command_buf, "%s%c", command, checksum);
	if (robofocConn->writePort (command_buf, 9))
		return -1;
	return 0;
}


int
Robofocus::setSwitch (int switch_num, int new_state)
{
	char command[10], rbuf[10];
	char command_buffer[9] = "FP001111";
	if (switch_num >= switchNum)
		return -1;
	int swstate = getSwitchState ();
	if (swstate < 0)
		return -1;
	for (int i = 0; i < switchNum; i++)
	{
		if (swstate & (1 << i))
			command_buffer[i + 4] = '2';
	}
	command_buffer[switch_num + 4] = (new_state == 1 ? '2' : '1');
	compute_checksum (command_buffer);
	sprintf (command, "%s%c", command_buffer, checksum);

	if (robofocConn->writeRead (command, 9, rbuf, 9) != 9)
		return -1;

	infoAll ();
	return 0;
}


int
Robofocus::focus_move (const char *cmd, int steps)
{
	char command[10];
	char command_buffer[9];

	// Number of steps moved must account for backlash compensation
	//  if (strcmp (cmd, CMD_FOCUS_MOVE_OUT) == 0)
	//    num_steps = steps + 40;
	//  else
	//    num_steps = steps;

	// Pad out command with leading zeros
	sprintf (command_buffer, "%s%06i", cmd, steps);

	//Get checksum character
	compute_checksum (command_buffer);

	sprintf (command, "%s%c", command_buffer, checksum);

	// Send command to focuser

	if (robofocConn->writePort (command, 9))
		return -1;

	step_num = steps;

	return 0;
}


int
Robofocus::isFocusing ()
{
	char rbuf[10];
	int ret;
	ret = robofocConn->readPort (rbuf, 1);
	if (ret == -1)
		return ret;
	// if we get F, read out end command
	if (*rbuf == 'F')
	{
		ret = robofocConn->readPort (rbuf + 1, 8);
		usleep (USEC_SEC/10);
		if (ret != 8)
			return -1;
		return -2;
	}
	return 0;
}


// Calculate checksum (according to RoboFocus spec.)
void
Robofocus::compute_checksum (char *cmd)
{
	int bytesum = 0;
	unsigned int size, i;

	size = strlen (cmd);

	for (i = 0; i < size; i++)
		bytesum = bytesum + (int) (cmd[i]);

	checksum = toascii ((bytesum % 340));
}


int
main (int argc, char **argv)
{
	Robofocus device = Robofocus (argc, argv);
	return device.run ();
}
