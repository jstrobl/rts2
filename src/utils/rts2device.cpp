#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <config.h>
#include "status.h"
#include "rts2device.h"
#include "rts2command.h"

#define MINDATAPORT		5556
#define MAXDATAPORT		5656

int
Rts2DevConn::commandAuthorized ()
{
  if (isCommand ("ready"))
    {
      return master->ready (this);
    }
  else if (isCommand ("info"))
    {
      return master->info (this);
    }
  else if (isCommand ("base_info"))
    {
      return master->baseInfo (this);
    }
  else if (isCommand ("killall"))
    {
      CHECK_PRIORITY;
      return master->killAll ();
    }
  else if (isCommand ("script_ends"))
    {
      CHECK_PRIORITY;
      return master->scriptEnds ();
    }
  // pseudo-command; will not be answered with ok ect..
  // as it can occur inside command block
  else if (isCommand ("this_device"))
    {
      char *deviceName;
      int deviceType;
      if (paramNextString (&deviceName)
	  || paramNextInteger (&deviceType) || !paramEnd ())
	return -2;
      setName (deviceName);
      setOtherType (deviceType);
      return -1;
    }
  // we need to try that - due to other device commands
  return -5;
}

int
Rts2DevConn::command ()
{
  int ret;
  if ((getCentraldId () != -1 && isConnState (CONN_AUTH_OK)) || getType () == DEVICE_DEVICE)	// authorized and running
    {
      ret = commandAuthorized ();
      if (ret == DEVDEM_E_HW)
	{
	  sendCommandEnd (DEVDEM_E_HW, "device error");
	  return -1;
	}
      if (ret != -5)
	return ret;
    }
  if (isCommand ("auth"))
    {
      int auth_id;
      int auth_key;
      if (paramNextInteger (&auth_id)
	  || paramNextInteger (&auth_key) || !paramEnd ())
	return -2;
      setCentraldId (auth_id);
      setKey (auth_key);
      ret = master->authorize (this);
      if (ret)
	{
	  sendCommandEnd (DEVDEM_E_SYSTEM,
			  "cannot authorize; try again later");
	  setConnState (CONN_AUTH_FAILED);
	  return -1;
	}
      setConnState (CONN_AUTH_PENDING);
      return -1;
    }
  return Rts2Conn::command ();
}

Rts2DevConn::Rts2DevConn (int in_sock, Rts2Device * in_master):Rts2Conn (in_sock,
	  in_master)
{
  address = NULL;
  master = in_master;
  if (in_sock > 0)
    setType (DEVICE_SERVER);
  else
    setType (DEVICE_DEVICE);
}

int
Rts2DevConn::init ()
{
  if (getType () != DEVICE_DEVICE)
    return Rts2Conn::init ();
  int ret;
  struct addrinfo *device_addr;
  if (!address)
    return -1;

  ret = address->getSockaddr (&device_addr);

  if (ret)
    {
      logStream (MESSAGE_ERROR) << "Rts2Address::getAddress getaddrinfor: " <<
	strerror (errno) << sendLog;
      return ret;
    }
  sock =
    socket (device_addr->ai_family, device_addr->ai_socktype,
	    device_addr->ai_protocol);
  if (sock == -1)
    {
      return -1;
    }
  ret = fcntl (sock, F_SETFL, O_NONBLOCK);
  if (ret == -1)
    {
      return -1;
    }
  ret = connect (sock, device_addr->ai_addr, device_addr->ai_addrlen);
  freeaddrinfo (device_addr);
  if (ret == -1)
    {
      if (errno == EINPROGRESS)
	{
	  setConnState (CONN_CONNECTING);
	  return 0;
	}
      return -1;
    }
  connAuth ();
  return 0;
}

int
Rts2DevConn::idle ()
{
  if (getType () != DEVICE_DEVICE)
    return Rts2Conn::idle ();

  if (isConnState (CONN_CONNECTING))
    {
      int err = 0;
      int ret;
      socklen_t len = sizeof (err);

      ret = getsockopt (sock, SOL_SOCKET, SO_ERROR, &err, &len);
      if (ret)
	{
#ifdef DEBUG_EXTRA
	  logStream (MESSAGE_ERROR) << "Rts2Dev2DevConn::idle getsockopt " <<
	    strerror (errno) << sendLog;
#endif
	  connectionError (-1);
	}
      else if (err)
	{
#ifdef DEBUG_EXTRA
	  logStream (MESSAGE_ERROR) << "Rts2Dev2DevConn::idle getsockopt " <<
	    strerror (err) << sendLog;
#endif
	  connectionError (-1);
	}
      else
	{
	  connAuth ();
	}
    }
  return Rts2Conn::idle ();
}

int
Rts2DevConn::authorizationOK ()
{
  setConnState (CONN_AUTH_OK);
  sendPriorityInfo ();
  master->baseInfo ();
  master->sendBaseInfo (this);
  master->info ();
  master->sendInfo (this);
  master->sendStateInfo (this);
  sendCommandEnd (0, "OK authorized");
  return 0;
}

int
Rts2DevConn::authorizationFailed ()
{
  setCentraldId (-1);
  setConnState (CONN_DELETE);
  sendCommandEnd (DEVDEM_E_SYSTEM, "authorization failed");
  logStream (MESSAGE_DEBUG) << "authorization failed: " << getName () <<
    sendLog;
  return 0;
}

void
Rts2DevConn::setHavePriority (int in_have_priority)
{
  if (havePriority () != in_have_priority)
    {
      Rts2Conn::setHavePriority (in_have_priority);
    }
}

void
Rts2DevConn::setDeviceAddress (Rts2Address * in_addr)
{
  address = in_addr;
  setConnState (CONN_CONNECTING);
  setName (in_addr->getName ());
  init ();
  setOtherType (address->getType ());
}

void
Rts2DevConn::setDeviceName (char *in_name)
{
  setName (in_name);
  setConnState (CONN_RESOLVING_DEVICE);
}

void
Rts2DevConn::connAuth ()
{
#ifdef DEBUG_EXTRA
  logStream (MESSAGE_DEBUG) << "auth: " << getName () << " state: " <<
    getConnState () << sendLog;
#endif
  master->getCentraldConn ()->
    queCommand (new Rts2CommandAuthorize (master, getName ()));
  setConnState (CONN_AUTH_PENDING);
}

void
Rts2DevConn::setKey (int in_key)
{
  Rts2Conn::setKey (in_key);
  if (getType () == DEVICE_DEVICE)
    {
      if (isConnState (CONN_AUTH_PENDING))
	{
	  // que to begining, send command
	  // kill all runinng commands
	  queSend (new Rts2CommandSendKey (master, in_key));
	}
      else
	{
#ifdef DEBUG_EXTRA
	  logStream (MESSAGE_DEBUG) <<
	    "Rts2DevConn::setKey invalid connection state: " <<
	    getConnState () << sendLog;
#endif
	}
    }
}

void
Rts2DevConn::setConnState (conn_state_t new_conn_state)
{
  if (getType () != DEVICE_DEVICE)
    return Rts2Conn::setConnState (new_conn_state);
  if (new_conn_state == CONN_AUTH_OK)
    {
      char *msg;
      asprintf (&msg, "this_device %s %i", master->getDeviceName (),
		master->getDeviceType ());
      send (msg);
      free (msg);
      master->sendMetaInfo (this);
      master->baseInfo (this);
      master->info (this);
      sendPriorityInfo ();
      master->sendStateInfo (this);
    }
  Rts2Conn::setConnState (new_conn_state);
}

Rts2DevConnMaster::Rts2DevConnMaster (Rts2Block * in_master,
				      char *in_device_host,
				      int in_device_port,
				      char *in_device_name,
				      int in_device_type,
				      char *in_master_host,
				      int in_master_port):
Rts2Conn (-1, in_master)
{
  device_host = in_device_host;
  device_port = in_device_port;
  strncpy (device_name, in_device_name, DEVICE_NAME_SIZE);
  device_type = in_device_type;
  strncpy (master_host, in_master_host, HOST_NAME_MAX);
  master_port = in_master_port;
}

Rts2DevConnMaster::~Rts2DevConnMaster (void)
{
}

int
Rts2DevConnMaster::registerDevice ()
{
  char *msg;
  int ret;
  if (!device_host)
    {
      device_host = new char[HOST_NAME_MAX];
      ret = gethostname (device_host, HOST_NAME_MAX);
      if (ret < 0)
	return -1;
    }
  asprintf (&msg, "register %s %i %s %i", device_name, device_type,
	    device_host, device_port);
  ret = send (msg);
  free (msg);
  return ret;
}

int
Rts2DevConnMaster::connectionError (int last_data_size)
{
  if (sock > 0)
    {
      close (sock);
      sock = -1;
    }
  if (!isConnState (CONN_BROKEN))
    {
      setConnState (CONN_BROKEN);
      master->centraldConnBroken ();
      time (&nextTime);
      nextTime += 60;
    }
  return -1;
}

int
Rts2DevConnMaster::init ()
{
  struct hostent *master_info;
  struct sockaddr_in address;
  int ret;
  // init sock address
  address.sin_family = AF_INET;
  address.sin_port = htons (master_port);
  // get remote host
  if ((master_info = gethostbyname (master_host)))
    {
      address.sin_addr = *(struct in_addr *) master_info->h_addr;
    }
  else
    {
      logStream (MESSAGE_ERROR) <<
	"Rts2DevConnMaster::init cannot get host name " << master_host << " "
	<< strerror (errno) << sendLog;
      return -1;
    }
  // get hostname
  sock = socket (PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    {
      logStream (MESSAGE_ERROR) <<
	"Rts2DevConnMaster::init cannot create socket: " << strerror (errno)
	<< sendLog;
      return -1;
    }
  ret = connect (sock, (struct sockaddr *) &address, sizeof (address));
  if (ret < 0)
    {
      logStream (MESSAGE_ERROR) << "Rts2DevConnMaster::init cannot connect: "
	<< strerror (errno) << sendLog;
      close (sock);
      sock = -1;
      return -1;
    }
  // have to wait for reply
  return registerDevice ();
}

int
Rts2DevConnMaster::idle ()
{
  time_t now;
  time (&now);
  switch (getConnState ())
    {
    case CONN_BROKEN:
      if (now > nextTime)
	{
	  nextTime = now + 60;
	  int ret;
	  ret = init ();
	  if (!ret)
	    setConnState (CONN_AUTH_OK);
	}
      break;
    default:
      break;
    }
  return Rts2Conn::idle ();
}

int
Rts2DevConnMaster::command ()
{
  Rts2Conn *auth_conn;
  if (isCommand ("A"))
    {
      char *a_type;
      int auth_id;
      if (paramNextString (&a_type))
	return -1;
      if (!strcmp ("authorization_ok", a_type))
	{
	  if (paramNextInteger (&auth_id) || !paramEnd ())
	    return -1;
	  // find connection with given ID
	  auth_conn = master->findCentralId (auth_id);
	  if (auth_conn)
	    auth_conn->authorizationOK ();
	  return -1;
	}
      else if (!strcmp ("authorization_failed", a_type))
	{
	  if (paramNextInteger (&auth_id) || !paramEnd ())
	    return -1;
	  // find connection with given ID
	  auth_conn = master->findCentralId (auth_id);
	  if (auth_conn)
	    auth_conn->authorizationFailed ();
	  return -1;
	}
      if (!strcmp ("registered_as", a_type))
	{
	  int clientId;
	  if (paramNextInteger (&clientId) || !paramEnd ())
	    return -2;
	  setCentraldId (clientId);
	  master->centraldConnRunning ();
	  return -1;
	}
    }
  if (isCommand ("authorization_key"))
    {
      char *p_device_name;
      int p_key;
      Rts2Conn *conn;
      if (paramNextString (&p_device_name)
	  || paramNextInteger (&p_key) || !paramEnd ())
	return -2;
      conn = master->getOpenConnection (p_device_name);
      if (conn)
	conn->setKey (p_key);
      return -1;
    }

  return Rts2Conn::command ();
}

int
Rts2DevConnMaster::priorityChange ()
{
  // change priority
  int priority_client;
  int timeout;
  if (paramNextInteger (&priority_client) || paramNextInteger (&timeout))
    return -2;
  master->setPriorityClient (priority_client, timeout);
  return -1;
}

int
Rts2DevConnMaster::informations ()
{
  int state_value;
  if (paramNextInteger (&state_value) || !paramEnd ())
    return -1;
  return master->setMasterState (state_value);
}

int
Rts2DevConnMaster::status ()
{
  int new_state;
  if (paramNextInteger (&new_state) || !paramEnd ())
    return -1;
  return master->setMasterState (new_state);
}

int
Rts2DevConnMaster::authorize (Rts2DevConn * conn)
{
  char *msg;
  int ret;
  asprintf (&msg, "authorize %i %i", conn->getCentraldId (), conn->getKey ());
  ret = send (msg);
  free (msg);
  return ret;
}

int
Rts2DevConnData::init ()
{
  // find empty port
  sock = socket (PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  struct sockaddr_in server;
  int test_port;

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl (INADDR_ANY);

  // find empty port
  for (test_port = MINDATAPORT; test_port < MAXDATAPORT; test_port++)
    {
      server.sin_port = htons (test_port);
      if (bind (sock, (struct sockaddr *) &server, sizeof (server)) == 0)
	break;
    }
  if (test_port == MAXDATAPORT)
    {
      close (sock);
      sock = -1;
      return -1;
    }

  if (listen (sock, 1))
    {
      close (sock);
      sock = -1;
      return -1;
    }

  setPort (test_port);

  setConnState (CONN_CONNECTING);
  return 0;
}

int
Rts2DevConnData::send (const char *msg)
{
  return 0;
}

int
Rts2DevConnData::send (char *data, size_t data_size)
{
  if (!isConnState (CONN_CONNECTED))
    return -2;
  return write (sock, data, data_size);
}

int
Rts2DevConnData::sendHeader ()
{
  return 0;
}

Rts2Device::Rts2Device (int in_argc, char **in_argv, int in_device_type, char *default_name):
Rts2Daemon (in_argc, in_argv)
{
  /* put defaults to variables.. */
  conn_master = NULL;

  device_name = default_name;
  centrald_host = "localhost";
  centrald_port = 617;
  log_option = 0;

  state = 0;

  device_type = in_device_type;

  device_host = NULL;

  mailAddress = NULL;

  // now add options..
  addOption ('L', "hostname", 1,
	     "hostname, if it different from return of gethostname()");
  addOption ('S', "centrald_host", 1,
	     "name of computer, on which central server runs");
  addOption ('P', "centrald_port", 1, "port number of central host");
  addOption ('M', "mail-to", 1, "send report mails to this adresses");
  addOption ('d', "device_name", 1, "name of device");
}

Rts2Device::~Rts2Device (void)
{
}

Rts2DevConn *
Rts2Device::createConnection (int in_sock)
{
  return new Rts2DevConn (in_sock, this);
}

int
Rts2Device::processOption (int in_opt)
{
  switch (in_opt)
    {
    case 'L':
      device_host = optarg;
      break;
    case 'S':
      centrald_host = optarg;
      break;
    case 'P':
      centrald_port = atoi (optarg);
      break;
    case 'M':
      mailAddress = optarg;
      break;
    case 'd':
      device_name = optarg;
      break;
    default:
      return Rts2Daemon::processOption (in_opt);
    }
  return 0;
}

void
Rts2Device::cancelPriorityOperations ()
{
  scriptEnds ();
}

void
Rts2Device::clearStatesPriority ()
{
  maskState (0xffffff | DEVICE_ERROR_MASK, DEVICE_ERROR_KILL,
	     "all operations canceled by priority");
}

Rts2Conn *
Rts2Device::createClientConnection (char *in_device_name)
{
  Rts2DevConn *conn;
  conn = createConnection (-1);
  conn->setDeviceName (in_device_name);
  return conn;
}

Rts2Conn *
Rts2Device::createClientConnection (Rts2Address * in_addres)
{
  Rts2DevConn *conn;
  if (in_addres->isAddress (device_name))
    return NULL;
  conn = createConnection (-1);
  conn->setDeviceAddress (in_addres);
  return conn;
}

void
Rts2Device::setState (int new_state, char *description)
{
  int ret;
  // state was set..do not set it again
  if (state == new_state)
    {
      return;
    }
  state = new_state;
  // try to wake-up qued changes..
  for (Rts2ValueQueVector::iterator iter = queValues.begin ();
       iter != queValues.end ();)
    {
      Rts2ValueQue *queVal = *iter;
      // free qued values
      if (!queValueChange (queVal->getCondValue ()))
	{
	  std::string newValStr =
	    std::string (queVal->getNewValue ()->getValue ());
	  ret =
	    setValue (queVal->getOldValue (), queVal->getOperation (),
		      queVal->getNewValue ());
	  if (ret)
	    logStream (MESSAGE_ERROR) << "cannot set qued value " << queVal->
	      getOldValue ()->getName () << " with operation " << queVal->
	      getOperation () << " and new value value " << newValStr <<
	      sendLog;
	  else
	    logStream (MESSAGE_DEBUG) << "change value of' " << queVal->
	      getOldValue ()->getName () << "' from que" << sendLog;
	  delete queVal;
	  iter = queValues.erase (iter);
	}
      else
	{
	  iter++;
	}
    }
  sendStatusMessage (state);
}

int
Rts2Device::sendStateInfo (Rts2Conn * conn)
{
  int ret;
  char *msg;
  asprintf (&msg, PROTO_INFO " %i", state);
  ret = conn->send (msg);
  free (msg);
  return ret;
}

int
Rts2Device::changeState (int new_state, char *description)
{
  setState (new_state, description);
  return 0;
}

int
Rts2Device::maskState (int state_mask, int new_state, char *description)
{
#ifdef DEBUG_EXTRA
  logStream (MESSAGE_DEBUG) <<
    "Rts2Device::maskState state: " << " state_mask: " <<
    state_mask << " new_state: " << new_state << " desc: " << description <<
    sendLog;
#endif
  int masked_state = state;
  // null from state all errors..
  masked_state &= ~(DEVICE_ERROR_MASK | state_mask);
  masked_state |= new_state;
  setState (masked_state, description);
  return 0;
}

int
Rts2Device::init ()
{
  int ret;
  char *lock_fname;

  // try to open log file..

  ret = Rts2Daemon::init ();
  if (ret)
    return ret;

  asprintf (&lock_fname, LOCK_PREFIX "%s", device_name);
  ret = checkLockFile (lock_fname);
  if (ret < 0)
    return ret;
  ret = doDeamonize ();
  if (ret)
    return ret;
  ret = lockFile ();
  if (ret)
    return ret;

  free (lock_fname);

  conn_master =
    new Rts2DevConnMaster (this, device_host, getPort (), device_name,
			   device_type, centrald_host, centrald_port);
  addConnection (conn_master);

  while (conn_master->init () < 0)
    {
      logStream (MESSAGE_WARNING) << "Rts2Device::init waiting for master" <<
	sendLog;
      sleep (60);
      if (getEndLoop ())
	return -1;
    }
  return ret;
}

int
Rts2Device::authorize (Rts2DevConn * conn)
{
  return conn_master->authorize (conn);
}

int
Rts2Device::ready ()
{
  return -1;
}

void
Rts2Device::sendMessage (messageType_t in_messageType,
			 const char *in_messageString)
{
  Rts2Daemon::sendMessage (in_messageType, in_messageString);
  Rts2Conn *centraldConn = getCentraldConn ();
  if (!centraldConn)
    return;
  Rts2Message msg =
    Rts2Message (getDeviceName (), in_messageType, in_messageString);
  centraldConn->sendMessage (msg);
}

int
Rts2Device::sendMail (char *subject, char *text)
{
  // no mail will be send
  if (!mailAddress)
    return 0;

  return sendMailTo (subject, text, mailAddress);
}

int
Rts2Device::killAll ()
{
  cancelPriorityOperations ();
  return 0;
}

int
Rts2Device::scriptEnds ()
{
  loadValues ();
  return 0;
}

int
Rts2Device::ready (Rts2Conn * conn)
{
  int ret;
  ret = ready ();
  if (ret)
    {
      conn->sendCommandEnd (DEVDEM_E_HW, "device not ready");
      return -1;
    }
  return 0;
}

void
Rts2Device::sigHUP (int sig)
{
  // do nothing
}
