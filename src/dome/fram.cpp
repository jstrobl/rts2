#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <mcheck.h>

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>


#include "dome.h"

#define BAUDRATE B9600
#define ZAPIS_NA_PORT 4
#define STAV_PORTU 0
#define CHYBA_NEZNAMY_PRIKAZ 0x80
#define CHYBA_PRETECENI_TX_BUFFERU 0x81
#define PORT_A 0
#define PORT_B 1
#define PORT_C 2

// check time in usec (1000000 ms = 1s)
#define FRAM_CHECK_TIMEOUT 1000

// following times are in seconds!
#define FRAM_TIME_OPEN_RIGHT	26
#define FRAM_TIME_OPEN_LEFT	24
#define FRAM_TIME_CLOSE_RIGHT	30
#define FRAM_TIME_CLOSE_LEFT	25

// how long we will keep lastWeatherStatus as actual (in second)
#define FRAM_WEATHER_TIMEOUT	60
// should be in 0-99 range, as 99 is maximum value which station can measure
#define FRAM_MAX_WINDSPEED      50
#define FRAM_MAX_PEAK_WINDSPEED 50

typedef enum
{ VENTIL_AKTIVACNI,
  VENTIL_OTEVIRANI_PRAVY,
  VENTIL_OTEVIRANI_LEVY,
  VENTIL_ZAVIRANI_PRAVY,
  VENTIL_ZAVIRANI_LEVY,
  KOMPRESOR,
  ZASUVKA_PRAVA,
  ZASUVKA_LEVA,
  KONCAK_OTEVRENI_PRAVY,
  KONCAK_OTEVRENI_LEVY,
  KONCAK_ZAVRENI_PRAVY,
  KONCAK_ZAVRENI_LEVY
}
vystupy;

struct typ_a
{
  unsigned char port;
  unsigned char pin;
} adresa[] =
{
  {
  PORT_A, 32},
  {
  PORT_B, 1},
  {
  PORT_B, 2},
  {
  PORT_B, 4},
  {
  PORT_B, 8},
  {
  PORT_A, 1},
  {
  PORT_A, 2},
  {
  PORT_A, 4},
  {
  PORT_B, 16},
  {
  PORT_B, 32},
  {
  PORT_B, 64},
  {
  PORT_B, 128}
};

#define NUM_ZAS		3

#define OFF		0
#define STANDBY		1
#define OBSERVING	2

// prvni zasuvka dalekohled, druha kamery, treti chlazeni
int zasuvky_index[NUM_ZAS] = { 0, 1, 2 };

enum stavy
{ ZAS_VYP, ZAS_ZAP };

// prvni index - cislo stavu (0 - off, 1 - standby, 2 - observing), druhy je stav zasuvky cislo j)
enum stavy zasuvky_stavy[3][NUM_ZAS] =
{
  // off
  {ZAS_VYP, ZAS_VYP, ZAS_VYP},
  // standby
  {ZAS_ZAP, ZAS_ZAP, ZAS_VYP},
  // observnig
  {ZAS_ZAP, ZAS_ZAP, ZAS_ZAP}
};

/**********************************************************************************************
 *
 * Class for weather info connection.
 *
 * Bind to specified port, send back responds packet, changed state
 * acordingly to weather service, close/open dome as well (using
 * master pointer)
 *
 * To be used in Pierre-Auger site in Argentina.
 * 
 *********************************************************************************************/

class Rts2ConnFramWeather:public Rts2Conn
{
private:
  Rts2DevDome * master;
  int weather_port;

  int rain;
  float windspeed;
  time_t lastWeatherStatus;

protected:

public:
    Rts2ConnFramWeather (int in_weather_port, Rts2DevDome * in_master);
  virtual int init ();
  virtual int receive (fd_set * set);
  // return 1 if weather is favourable to open dome..
  virtual int isGoodWeather ();
};

Rts2ConnFramWeather::Rts2ConnFramWeather (int in_weather_port,
					  Rts2DevDome * in_master):
Rts2Conn (in_master)
{
  master = in_master;
  weather_port = in_weather_port;
}

int
Rts2ConnFramWeather::init ()
{
  struct sockaddr_in bind_addr;
  int optval = 1;
  int ret;

  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons (weather_port);
  bind_addr.sin_addr.s_addr = htonl (INADDR_ANY);

  sock = socket (PF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      syslog (LOG_ERR, "Rts2ConnFramWeather::init socket: %m");
      return -1;
    }
  ret = fcntl (sock, F_SETFL, O_NONBLOCK);
  if (ret)
    {
      syslog (LOG_ERR, "Rts2ConnFramWeather::init fcntl: %m");
      return -1;
    }
  ret = bind (sock, (struct sockaddr *) &bind_addr, sizeof (bind_addr));
  if (ret)
    {
      syslog (LOG_ERR, "Rts2ConnFramWeather::init bind: %m");
    }
  return ret;
}

int
Rts2ConnFramWeather::receive (fd_set * set)
{
  int ret;
  char buf[100];
  char status[10];
  int data_size = 0;
  struct tm statDate;
  float sec_f;
  if (sock >= 0 && FD_ISSET (sock, set))
    {
      struct sockaddr_in from;
      socklen_t size = sizeof (from);
      data_size =
	recvfrom (sock, buf, 80, 0, (struct sockaddr *) &from, &size);
      buf[data_size] = 0;
      syslog (LOG_DEBUG, "readed: %i %s from: %s:%i", data_size, buf,
	      inet_ntoa (from.sin_addr), ntohs (from.sin_port));
      // parse weather info
      ret =
	sscanf (buf,
		"windspeed=%f km/h rain=%i date=%i-%u-%u time=%u:%u:%f status=%s",
		&windspeed, &rain, &statDate.tm_year, &statDate.tm_mon,
		&statDate.tm_mday, &statDate.tm_hour, &statDate.tm_min,
		&sec_f, status);
      if (ret != 9)
	{
	  syslog (LOG_ERR, "sscanf on udp data returned: %i", ret);
	  return data_size;
	}
      statDate.tm_isdst = 0;
      statDate.tm_year -= 1900;
      statDate.tm_mon--;
      statDate.tm_sec = (int) sec_f;
      lastWeatherStatus = mktime (&statDate);
      if (strcmp (status, "watch"))
	{
	  // if sensors doesn't work, switch rain on
	  rain = 1;
	}
      syslog (LOG_DEBUG, "windspeed: %f rain: %i date: %i status: %s",
	      windspeed, rain, lastWeatherStatus, status);
      if (rain != 0 || windspeed > FRAM_MAX_PEAK_WINDSPEED)
	{
	  master->closeDome ();
	  master->setMasterStandby ();
	}
      // ack message
      ret =
	sendto (sock, "ack", 3, 0, (struct sockaddr *) &from, sizeof (from));
    }
  return data_size;
}

int
Rts2ConnFramWeather::isGoodWeather ()
{
  time_t now;
  time (&now);
  if (windspeed > FRAM_MAX_WINDSPEED || rain != 0
      || (now - lastWeatherStatus > FRAM_WEATHER_TIMEOUT))
    return 0;
  return 1;
}

/***********************************************************************************************
 * 
 * Class for dome controller.
 *
 **********************************************************************************************/

class Rts2DevDomeFram:public Rts2DevDome
{
private:
  int dome_port;
  char *dome_file;

  unsigned char spinac[2];
  unsigned char stav_portu[3];

  Rts2ConnFramWeather *weatherConn;

  int zjisti_stav_portu ();
  void zapni_pin (unsigned char c_port, unsigned char pin);
  void vypni_pin (unsigned char c_port, unsigned char pin);
  int getPortState (int c_port)
  {
    return !!(stav_portu[adresa[c_port].port] & adresa[c_port].pin);
  };

  int isOn (int c_port);
  int handle_zasuvky (int state);

  int openLeft ();
  int openRight ();

  int closeRight ();
  int closeLeft ();

  int stopMove ();

  time_t timeoutEnd;

  void setMotorTimeout (time_t timeout);

  /** 
   * Check if timeout was exceeded.
   *
   * @return 0 when timeout wasn't reached, 1 when timeout was
   * exceeded.
   */

  int checkMotorTimeout ();

  enum
  { MOVE_NONE, MOVE_OPEN_LEFT, MOVE_OPEN_LEFT_WAIT, MOVE_OPEN_RIGHT,
    MOVE_OPEN_RIGHT_WAIT,
    MOVE_CLOSE_RIGHT, MOVE_CLOSE_RIGHT_WAIT, MOVE_CLOSE_LEFT,
    MOVE_CLOSE_LEFT_WAIT
  } movingState;
public:
  Rts2DevDomeFram (int argc, char **argv);
  virtual ~ Rts2DevDomeFram (void);
  virtual int processOption (int in_opt);
  virtual int init ();
  virtual int idle ();

  virtual int ready ();
  virtual int baseInfo ();
  virtual int info ();

  virtual int openDome ();
  virtual long isOpened ();
  virtual int endOpen ();
  virtual int closeDome ();
  virtual long isClosed ();
  virtual int endClose ();

  virtual int observing ();
  virtual int standby ();
  virtual int off ();
};

int
Rts2DevDomeFram::zjisti_stav_portu ()
{
  unsigned char t, c = STAV_PORTU | PORT_A;
  int ret;
  write (dome_port, &c, 1);
  if (read (dome_port, &t, 1) < 1)
    syslog (LOG_ERR, "read error 0");
  else
    syslog (LOG_DEBUG, "stav: A: %x:", t);
  read (dome_port, &stav_portu[PORT_A], 1);
  syslog (LOG_DEBUG, "A state: %x", stav_portu[PORT_A]);
  c = STAV_PORTU | PORT_B;
  write (dome_port, &c, 1);
  if (read (dome_port, &t, 1) < 1)
    syslog (LOG_ERR, "read error 1");
  else
    syslog (LOG_DEBUG, " B: %x:", t);
  ret = read (dome_port, &stav_portu[PORT_B], 1);
  syslog (LOG_DEBUG, "B state: %x", stav_portu[PORT_B]);
  if (ret < 1)
    return -1;
  return 0;
}

void
Rts2DevDomeFram::zapni_pin (unsigned char port, unsigned char pin)
{
  unsigned char c;
  zjisti_stav_portu ();
  c = ZAPIS_NA_PORT | port;
  syslog (LOG_DEBUG, "port:%xh pin:%xh write: %x:", port, pin, c);
  write (dome_port, &c, 1);
  c = stav_portu[port] | pin;
  syslog (LOG_DEBUG, "zapni_pin: %xh", c);
  write (dome_port, &c, 1);
}

void
Rts2DevDomeFram::vypni_pin (unsigned char port, unsigned char pin)
{
  unsigned char c;
  zjisti_stav_portu ();
  c = ZAPIS_NA_PORT | port;
  syslog (LOG_DEBUG, "port:%xh pin:%xh write: %x:", port, pin, c);
  write (dome_port, &c, 1);
  c = stav_portu[port] & (~pin);
  syslog (LOG_DEBUG, "%xh", c);
  write (dome_port, &c, 1);
}

int
Rts2DevDomeFram::isOn (int c_port)
{
  zjisti_stav_portu ();
  return !(stav_portu[adresa[c_port].port] & adresa[c_port].pin);
}

void
Rts2DevDomeFram::setMotorTimeout (time_t timeout)
{
  time_t now;
  time (&now);
  timeoutEnd = now + timeout;
}

int
Rts2DevDomeFram::checkMotorTimeout ()
{
  time_t now;
  time (&now);
  if (now >= timeoutEnd)
    syslog (LOG_DEBUG, "timeout reached: %i state: %i", now - timeoutEnd,
	    movingState);
  return (now >= timeoutEnd);
}

#define ZAP(i) zapni_pin(adresa[i].port,adresa[i].pin)
#define VYP(i) vypni_pin(adresa[i].port,adresa[i].pin)

int
Rts2DevDomeFram::openLeft ()
{
  movingState = MOVE_OPEN_LEFT;
  ZAP (KOMPRESOR);
  sleep (1);
  syslog (LOG_DEBUG, "opening left door");
  ZAP (VENTIL_OTEVIRANI_LEVY);
  ZAP (VENTIL_AKTIVACNI);
  movingState = MOVE_OPEN_LEFT_WAIT;
  setMotorTimeout (FRAM_TIME_OPEN_LEFT);
  return 0;
}

int
Rts2DevDomeFram::openRight ()
{
  // otevri pravou strechu
  movingState = MOVE_OPEN_RIGHT;
  VYP (VENTIL_AKTIVACNI);
  VYP (VENTIL_OTEVIRANI_LEVY);
  ZAP (KOMPRESOR);
  sleep (1);
  syslog (LOG_DEBUG, "opening right door");
  ZAP (VENTIL_OTEVIRANI_PRAVY);
  ZAP (VENTIL_AKTIVACNI);
  movingState = MOVE_OPEN_RIGHT_WAIT;
  setMotorTimeout (FRAM_TIME_OPEN_RIGHT);
  return 0;
}

int
Rts2DevDomeFram::closeRight ()
{
  ZAP (KOMPRESOR);
  sleep (1);
  syslog (LOG_DEBUG, "closing right door");
  ZAP (VENTIL_ZAVIRANI_PRAVY);
  ZAP (VENTIL_AKTIVACNI);
  movingState = MOVE_CLOSE_RIGHT_WAIT;
  setMotorTimeout (FRAM_TIME_CLOSE_RIGHT);
  return 0;
}

int
Rts2DevDomeFram::closeLeft ()
{
  VYP (VENTIL_AKTIVACNI);
  VYP (VENTIL_ZAVIRANI_PRAVY);
  ZAP (KOMPRESOR);
  sleep (1);
  syslog (LOG_DEBUG, "closing left door");
  ZAP (VENTIL_ZAVIRANI_LEVY);
  ZAP (VENTIL_AKTIVACNI);
  movingState = MOVE_CLOSE_LEFT_WAIT;
  setMotorTimeout (FRAM_TIME_CLOSE_LEFT);
  return 0;
}

int
Rts2DevDomeFram::stopMove ()
{
  vypni_pin (adresa[VENTIL_AKTIVACNI].port,
	     adresa[VENTIL_AKTIVACNI].pin | adresa[KOMPRESOR].pin);
  movingState = MOVE_NONE;
  return 0;
}

int
Rts2DevDomeFram::openDome ()
{
  if (movingState != MOVE_NONE)
    return -1;
  if (!weatherConn->isGoodWeather ())
    return -1;
  if (isOn (KONCAK_OTEVRENI_LEVY) && isOn (KONCAK_OTEVRENI_LEVY))
    {
      return endOpen ();
    }

  vypni_pin (adresa[VENTIL_OTEVIRANI_PRAVY].port,
	     adresa[VENTIL_OTEVIRANI_PRAVY].pin
	     | adresa[VENTIL_ZAVIRANI_PRAVY].pin
	     | adresa[VENTIL_ZAVIRANI_LEVY].pin);

  if (!isOn (KONCAK_OTEVRENI_LEVY))
    {
      // otevri levou strechu
      openLeft ();
    }
  else				// like !isOn (KONCAK_OTEVRENI_PRAVY - see && at begining
    {
      openRight ();
    }

  setTimeout (10);
  return Rts2DevDome::openDome ();
}

long
Rts2DevDomeFram::isOpened ()
{
  syslog (LOG_DEBUG, "isOpened %i", movingState);
  switch (movingState)
    {
    case MOVE_OPEN_LEFT_WAIT:
      // check for timeout..
      if (!(isOn (KONCAK_OTEVRENI_LEVY) || checkMotorTimeout ()))
	// go to end return..
	break;
      // follow on..
    case MOVE_OPEN_RIGHT:
      openRight ();
      break;
    case MOVE_OPEN_RIGHT_WAIT:
      if (!(isOn (KONCAK_OTEVRENI_PRAVY) || checkMotorTimeout ()))
	break;
    default:
      return -2;
    }
  return FRAM_CHECK_TIMEOUT;
}

int
Rts2DevDomeFram::endOpen ()
{
  stopMove ();
  VYP (VENTIL_OTEVIRANI_PRAVY);
  VYP (VENTIL_OTEVIRANI_LEVY);
  return Rts2DevDome::endOpen ();
}

int
Rts2DevDomeFram::closeDome ()
{
  if (movingState == MOVE_CLOSE_RIGHT
      || movingState == MOVE_CLOSE_RIGHT_WAIT
      || movingState == MOVE_CLOSE_LEFT
      || movingState == MOVE_CLOSE_LEFT_WAIT)
    {
      // closing already in progress
      return 0;
    }
  if (zjisti_stav_portu () == -1)
    {
      return -1;
    }
  if (movingState != MOVE_NONE)
    {
      stopMove ();
    }
  if (isOn (KONCAK_ZAVRENI_PRAVY) && isOn (KONCAK_ZAVRENI_LEVY))
    return endClose ();

  vypni_pin (adresa[VENTIL_OTEVIRANI_PRAVY].port,
	     adresa[VENTIL_OTEVIRANI_PRAVY].pin
	     | adresa[VENTIL_ZAVIRANI_LEVY].pin
	     | adresa[VENTIL_OTEVIRANI_LEVY].pin);
  if (!isOn (KONCAK_ZAVRENI_PRAVY))
    {
      closeRight ();
    }
  else
    {
      closeLeft ();
    }
  return Rts2DevDome::closeDome ();
}

long
Rts2DevDomeFram::isClosed ()
{
  syslog (LOG_DEBUG, "isClosed %i", movingState);
  switch (movingState)
    {
    case MOVE_CLOSE_RIGHT_WAIT:
      if (!(isOn (KONCAK_ZAVRENI_PRAVY) || checkMotorTimeout ()))
	break;
      // close dome..
    case MOVE_CLOSE_LEFT:
      closeLeft ();
      break;
    case MOVE_CLOSE_LEFT_WAIT:
      if (!(isOn (KONCAK_ZAVRENI_LEVY) || checkMotorTimeout ()))
	break;
    default:
      return -2;
    }
  return FRAM_CHECK_TIMEOUT;
}

int
Rts2DevDomeFram::endClose ()
{
  stopMove ();
  VYP (VENTIL_ZAVIRANI_LEVY);
  VYP (VENTIL_ZAVIRANI_PRAVY);
  zjisti_stav_portu ();		//kdyz se to vynecha, neposle to posledni prikaz nebo znak
  return Rts2DevDome::endClose ();
}

int
Rts2DevDomeFram::processOption (int in_opt)
{
  switch (in_opt)
    {
    case 'f':
      dome_file = optarg;
      break;
    default:
      return Rts2DevDome::processOption (in_opt);
    }
  return 0;
}

int
Rts2DevDomeFram::init ()
{
  struct termios oldtio, newtio;
  int i;

  int ret = Rts2DevDome::init ();
  if (ret)
    return ret;

  dome_port = open (dome_file, O_RDWR | O_NOCTTY);
  if (dome_port == -1)
    return -1;

  tcgetattr (dome_port, &oldtio);

  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;
  newtio.c_lflag = 0;
  newtio.c_cc[VMIN] = 0;
  newtio.c_cc[VTIME] = 1;

  tcflush (dome_port, TCIOFLUSH);
  tcsetattr (dome_port, TCSANOW, &newtio);

  vypni_pin (adresa[VENTIL_AKTIVACNI].port,
	     adresa[VENTIL_AKTIVACNI].pin | adresa[KOMPRESOR].pin);

  movingState = MOVE_NONE;

  for (i = 0; i < MAX_CONN; i++)
    {
      if (!connections[i])
	{
	  weatherConn = new Rts2ConnFramWeather (5002, this);
	  weatherConn->init ();
	  connections[i] = weatherConn;
	  break;
	}
    }
  if (i == MAX_CONN)
    {
      syslog (LOG_ERR, "no free conn for Rts2ConnFramWeather");
      return -1;
    }

  return 0;
}

int
Rts2DevDomeFram::idle ()
{
  // check for weather..
  if (weatherConn->isGoodWeather () && observingPossible)
    {
      openDome ();
      sendMaster ("on");
    }
  else
    {
      int ret;
      ret = closeDome ();
      if (ret == -1)
	{
	  setTimeout (10 * USEC_SEC);
	}
      setMasterStandby ();
    }
  return Rts2DevDome::idle ();
}

Rts2DevDomeFram::Rts2DevDomeFram (int argc, char **argv):Rts2DevDome (argc,
	     argv)
{
  addOption ('f', "dome_file", 1, "/dev file for dome serial port");
  dome_file = "/dev/ttyS0";

  domeModel = "FRAM_FORD_2";

  movingState = MOVE_NONE;

  weatherConn = NULL;
}

Rts2DevDomeFram::~Rts2DevDomeFram (void)
{
  close (dome_port);
}

int
Rts2DevDomeFram::handle_zasuvky (int state)
{
  int i;
  for (i = 0; i < NUM_ZAS; i++)
    {
      int zasuvka_num = zasuvky_index[i];
      if (zasuvky_stavy[state][i] == ZAS_VYP)
	{
	  vypni_pin (adresa[zasuvka_num].port, adresa[zasuvka_num].pin);
	}
      else
	{
	  zapni_pin (adresa[zasuvka_num].port, adresa[zasuvka_num].pin);
	}
    }
  return 0;
}

int
Rts2DevDomeFram::ready ()
{
  return 0;
}

int
Rts2DevDomeFram::info ()
{
  int ret;
  ret = zjisti_stav_portu ();
  if (ret)
    return -1;
  sw_state = getPortState (KONCAK_OTEVRENI_PRAVY);
  sw_state |= (getPortState (KONCAK_OTEVRENI_LEVY) << 1);
  sw_state |= (getPortState (KONCAK_ZAVRENI_PRAVY) << 2);
  sw_state |= (getPortState (KONCAK_ZAVRENI_LEVY) << 3);
  return 0;
}

int
Rts2DevDomeFram::baseInfo ()
{
  return 0;
}

int
Rts2DevDomeFram::off ()
{
  closeDome ();
  //handle_zasuvky (OFF);
  return 0;
}

int
Rts2DevDomeFram::standby ()
{
  //handle_zasuvky (STANDBY);
  closeDome ();
  return 0;
}

int
Rts2DevDomeFram::observing ()
{
  //handle_zasuvky (OBSERVING);
  openDome ();
  return 0;
}

int
main (int argc, char **argv)
{
  mtrace ();

  Rts2DevDomeFram *device = new Rts2DevDomeFram (argc, argv);

  int ret;
  ret = device->init ();
  if (ret)
    {
      fprintf (stderr, "Cannot initialize dome - exiting!\n");
      exit (0);
    }
  device->run ();
  delete device;
}
