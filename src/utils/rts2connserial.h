/* 
 * Generic serial port connection.
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

#include "rts2connnosend.h"
#include <termios.h>

/**
 * Enum for baud speeds.
 */
typedef enum
{BS2400, BS4800, BS9600, BS19200, BS57600, BS115200}
bSpeedT;

/**
 * Enum for data size.
 */
typedef enum
{C7, C8}
cSizeT;

/**
 * Enum for parity.
 */
typedef enum
{NONE, ODD, EVEN}
parityT;

/**
 * Serial connection class.
 *
 * This class present single interface to set correctly serial port connection with
 * ussual parameters - baud speed, stop bits, and parity. It also have common functions
 * to do I/O on serial port, with proper error reporting throught logStream() mechanism.
 *
 * @ingroup RTS2Block
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class Rts2ConnSerial: public Rts2ConnNoSend
{
	private:
		struct termios s_termios;

		bSpeedT baudSpeed;

		cSizeT cSize;
		parityT parity;

		int vMin;
		int vTime;

		// if we will preint port communication
		bool debugPortComm;

		/**
		 * Returns baud speed as string.
		 */
		const char *getBaudSpeed ();

		int getVMin ()
		{
			return vMin;
		}

		int getVTime ()
		{
			return vTime;
		}

		// set s_termios to port..
		int setAttr ();
	public:
		/**
		 * Create connection to serial port.
		 *
		 * @param _devName   Device name (ussually /dev/ttySx)
		 * @param _master    Controlling block
		 * @param _baudSpeed Device baud speed
		 * @param _cSize     Character bits size (7 or 8 bits)
		 * @param _parity    Device parity.
		 * @param _vTime     Time to wait for single read before giving up.
		 */
		Rts2ConnSerial (const char *_devName, Rts2Block * _master,
			bSpeedT _baudSpeed = BS9600, cSizeT _cSize = C8,
			parityT _parity = NONE, int _vTime = 40);

		/**
		 * Init serial port.
		 *
		 * @return -1 on error, 0 on success.
		 */
		virtual int init ();

		/**
		 * Set socket vtime. VTIME is used with a serial port
		 * to specify time in decaseconds, for how long device will wait 
		 * during read call for character from serial port.
		 *
		 * @param _vtime New VTIME value (in decaseconds).
		 */
		int setVTime (int _vtime);

		/**
		 * Write single character to serial port.
		 *
		 * @param ch Character to write.
		 * @return -1 on error, 0 on sucess.
		 */
		int writePort (char ch);

		/**
		 * Write data to serial port.
		 *
		 * @param buf Buffer which will be written to the port.
		 * @param b_len Lenght of buffer to write on the port.
		 *
		 * @return -1 on error, 0 on sucess.
		 */
		int writePort (const char *wbuf, int b_len);

		/**
		 * Raw port read.
		 *
		 * Read one character from the port.
		 *
		 * @param ch Character which will be read.
		 * @return -1 on error, 1 on sucess.
		 */
		int readPort (char &ch);

		/**
		 * Raw read data from serial port.
		 *
		 * @param buf Buffer where data will be readed.
		 * @param b_len Lenght of buffer to read data.
		 *
		 * @return -1 on error, number of bytes readed on success.
		 */
		int readPort (char *rbuf, int b_len);

		/**
		 * Read data from serial port until end character is encountered.
		 *
		 * Read data until readed byte is not equal endChar and readed length is less then b_len.
		 *
		 * @param buf     Buffer where data will be readed.
		 * @param b_len   Lenght of buffer to read data.
		 * @param endChar End character.
		 *
		 * @return -1 on error, number of bytes readed on success.
		 */
		int readPort (char *rbuf, int b_len, char endChar);

		/**
		 * Flush serial port (make sure all data were sended and received).
		 */
		int flushPortIO ();

		/**
		 * Flush serial port output (make sure that all data were written)
		 */
		int flushPortO ();

		/**
		 * Set if debug messages from port communication will be printed.
		 *
		 * @param printDebug  True if all port communication should be written to log.
		 */
		void setDebug (bool printDebug = true)
		{
			debugPortComm = printDebug;
		}

		/**
		 * Exchange packets - write to port and read reply.
		 *
		 * This call combines Rts2ConnSerial::writePort ()
		 * and Rts2ConnSerial::readPort() calls to form a complete
		 * packet exchange.
		 *
		 * @param wbuf Buffer with data to write.
		 * @param wlne Length of the write buffer.
		 * @param rbuf Buffer where readed data will be stored.
		 * @param rlen Length of the read buffer.
		 *
		 * @return -1 on error, size of data readed on success.
		 */
		int writeRead (const char* wbuf, int wlen, char *rbuf, int rlen);

		/**
		 * Exchange packets - write to port and read reply.
		 *
		 * This call combines Rts2ConnSerial::writePort ()
		 * and Rts2ConnSerial::readPort() calls to form a complete
		 * packet exchange. Read continues while readed buffer is less then
		 * rbuf and while endChar is not encountered.
		 *
		 * @param wbuf    Buffer with data to write.
		 * @param wlne    Length of the write buffer.
		 * @param rbuf    Buffer where readed data will be stored.
		 * @param rlen    Length of the read buffer.
		 * @param endChar End character.
		 *
		 * @return -1 on error, size of data readed on success.
		 */
		int writeRead (const char* wbuf, int wlen, char *rbuf, int rlen, char endChar);
};
