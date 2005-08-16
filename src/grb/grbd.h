#ifndef __RTS2_GRBD__
#define __RTS2_GRBD__

#include "../utilsdb/rts2devicedb.h"
#include "rts2conngrb.h"

class Rts2ConnGrb;

class Rts2DevGrb:public Rts2DeviceDb
{
private:
  Rts2ConnGrb * gcncnn;
  char *gcn_host;
  int gcn_port;
  int do_hete_test;
public:
    Rts2DevGrb (int argc, char **argv);
    virtual ~ Rts2DevGrb ();
  virtual int processOption (int in_opt);
  virtual int init ();
  virtual Rts2DevConn *createConnection (int in_sock, int conn_num);

  virtual int ready ()
  {

  }
  virtual int baseInfo ()
  {
    return 0;
  }
  virtual int info ()
  {
    return 0;
  }
  virtual int sendInfo (Rts2Conn * conn);
  virtual int sendBaseInfo (Rts2Conn * conn)
  {
    return 0;
  }

  int newGcnGrb (int tar_id);
};

#endif /* __RTS2_GRBD__ */
