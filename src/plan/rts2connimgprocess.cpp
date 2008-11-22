/*
 * Connections for image processing forked instances.
 * Copyright (C) 2003-2008 Petr Kubanek <petr@kubanek.net>
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

#include "rts2connimgprocess.h"
#include "rts2script.h"

#include "../utils/rts2command.h"
#include "../utilsdb/rts2taruser.h"
#include "../utilsdb/rts2obs.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <sstream>

Rts2ConnProcess::Rts2ConnProcess (Rts2Block * in_master, const char *in_exe, int in_timeout):
Rts2ConnFork (in_master, in_exe, in_timeout)
{
}


Rts2ConnImgProcess::Rts2ConnImgProcess (Rts2Block * in_master,
const char *in_exe,
const char *in_path, int in_timeout):
Rts2ConnProcess (in_master, in_exe, in_timeout)
{
	imgPath = new char[strlen (in_path) + 1];
	strcpy (imgPath, in_path);
	astrometryStat = NOT_ASTROMETRY;
}


Rts2ConnImgProcess::~Rts2ConnImgProcess (void)
{
	delete[]imgPath;
}


int
Rts2ConnImgProcess::newProcess ()
{
	Rts2Image *image;

	#ifdef DEBUG_EXTRA
	logStream (MESSAGE_DEBUG) << "Rts2ConnImgProcess::newProcess exe: " <<
		exePath << " img: " << imgPath << " (" << getpid () << ")" << sendLog;
	#endif

	image = new Rts2Image (imgPath);
	if (image->getShutter () == SHUT_CLOSED)
	{
		astrometryStat = DARK;
		delete image;
		return 0;
	}
	delete image;

	if (exePath)
	{
		execl (exePath, exePath, imgPath, (char *) NULL);
		logStream (MESSAGE_ERROR) << "Rts2ConnImgProcess::newProcess: " <<
			strerror (errno) << sendLog;
	}
	return -2;
}


int
Rts2ConnImgProcess::processLine ()
{
	int ret;
	ret = sscanf (getCommand (),
		"%li %lf %lf (%lf,%lf)",
		&id, &ra, &dec, &ra_err, &dec_err);

	if (ret == 5)
	{
		astrometryStat = GET;
		// inform others..
	}
	#ifndef DEBUG_EXTRA
	else
	#endif						 /* !DEBUG_EXTRA */
		logStream (MESSAGE_DEBUG) << "receive: " << getCommand () << " sscanf: "
			<< ret << sendLog;
	return -1;
}


void
Rts2ConnImgProcess::connectionError (int last_data_size)
{
	int ret;
	const char *telescopeName;
	int corr_mark, corr_img;
	Rts2ImageSkyDb *image;

	if (last_data_size < 0 && errno == EAGAIN)
	{
		logStream (MESSAGE_DEBUG) << "Rts2ConnImgProcess::connectionError " <<
			strerror (errno) << " #" << errno << " last_data_size " <<
			last_data_size << sendLog;
		return;
	}

	image = new Rts2ImageSkyDb (imgPath);
	switch (astrometryStat)
	{
		case NOT_ASTROMETRY:
		case TRASH:
			astrometryStat = TRASH;
			image->toTrash ();
			sendProcEndMail (image);
			break;
		case GET:
			image->setAstroResults (ra, dec, ra_err / 60.0, dec_err / 60.0);
			image->toArchive ();
			// send correction to telescope..
			telescopeName = image->getMountName ();
			ret = image->getValue ("MOVE_NUM", corr_mark);
			if (ret)
				break;
			ret = image->getValue ("CORR_IMG", corr_img);
			if (ret)
				break;
			if (telescopeName)
			{
				Rts2Conn *telConn;
				telConn = master->findName (telescopeName);
				// correction error should be in degrees
				if (telConn)
				{
					struct ln_equ_posn pos1, pos2;
					pos1.ra = ra;
					pos1.dec = dec;

					pos2.ra = ra - ra_err / 60.0;
					pos2.dec = dec - dec_err / 60.0;

					double posErr = ln_get_angular_separation (&pos1, &pos2);

					telConn->queCommand (
						new Rts2CommandCorrect (master, corr_mark,
						corr_img, image->getImgId (),
						ra_err / 60.0, dec_err / 60.0, posErr)
						);
				}
			}
			sendOKMail (image);
			sendProcEndMail (image);
			break;
		case DARK:
			image->toDark ();
			break;
		default:
			break;
	}
	if (astrometryStat == GET)
		master->postEvent (new Rts2Event (EVENT_OK_ASTROMETRY, (void *) image));
	else
		master->postEvent (new Rts2Event (EVENT_NOT_ASTROMETRY, (void *) image));
	delete image;
	Rts2ConnFork::connectionError (last_data_size);
}


void
Rts2ConnImgProcess::sendOKMail (Rts2ImageDb * image)
{
	// is first such image..
	if (image->getOKCount () == 1)
	{
		int count;
		Rts2TarUser tar_user =
			Rts2TarUser (image->getTargetId (), image->getTargetType ());
		std::string mails = tar_user.getUsers (SEND_ASTRO_OK, count);
		if (count == 0)
			return;
		std::ostringstream subject;
		subject << "TARGET #"
			<< image->getTargetIdSel ()
			<< " (" << image->getTargetId ()
			<< ") GET ASTROMETRY (IMG_ID #" << image->getImgId () << ")";
		std::ostringstream os;
		os << *image;
		master->sendMailTo (subject.str ().c_str (), os.str ().c_str (),
			mails.c_str ());
	}
}


void
Rts2ConnImgProcess::sendProcEndMail (Rts2ImageDb * image)
{
	int ret;
	int obsId;
	// last processed
	obsId = image->getObsId ();
	Rts2Obs observation = Rts2Obs (obsId);
	ret = observation.checkUnprocessedImages (master);
	if (ret == 0)
	{
		// que as
		getMaster ()->
			postEvent (new Rts2Event (EVENT_ALL_PROCESSED, (void *) &obsId));
	}
}


Rts2ConnObsProcess::Rts2ConnObsProcess (Rts2Block * in_master,
const char *in_exe, int in_obsId,
int in_timeout):
Rts2ConnProcess (in_master, in_exe, in_timeout)
{
	obsId = in_obsId;
	obs = new Rts2Obs (obsId);
	if (obs->load ())
	{
		logStream (MESSAGE_ERROR) <<
			"Rts2ConnObsProcess::newProcess cannot load obs " << obsId << sendLog;
		obs = NULL;
	}

	asprintf (&obsIdCh, "%i", obsId);
	asprintf (&obsTarIdCh, "%i", obs->getTargetId ());
	asprintf (&obsTarTypeCh, "%c", obs->getTargetType ());

	delete obs;
}


int
Rts2ConnObsProcess::newProcess ()
{
	#ifdef DEBUG_EXTRA
	logStream (MESSAGE_DEBUG) << "Rts2ConnObsProcess::newProcess exe: " <<
		exePath << " obsid: " << obsId << " pid: " << getpid () << sendLog;
	#endif

	if (exePath)
	{
		execl (exePath, exePath, obsIdCh, obsTarIdCh, obsTarTypeCh,
			(char *) NULL);
		// if we get there, it's error in execl
		logStream (MESSAGE_ERROR) << "Rts2ConnObsProcess::newProcess: " <<
			strerror (errno) << sendLog;
	}
	return -2;
}


int
Rts2ConnObsProcess::processLine ()
{
	// no error
	return -1;
}


Rts2ConnDarkProcess::Rts2ConnDarkProcess (Rts2Block * in_master, const char *in_exe, int in_timeout)
:Rts2ConnProcess (in_master, in_exe, in_timeout)
{
}


int
Rts2ConnDarkProcess::processLine ()
{
	return -1;
}


Rts2ConnFlatProcess::Rts2ConnFlatProcess (Rts2Block * in_master, const char *in_exe, int in_timeout)
:Rts2ConnProcess (in_master, in_exe, in_timeout)
{
}


int
Rts2ConnFlatProcess::processLine ()
{
	return -1;
}
