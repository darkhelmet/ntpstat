/* name:                  ntpstat.c
   description:           show network time protocol (ntp) status
   author:                G.Richard Keech  <rkeech@redhat.com>
   date:                  2001-06-22
   (C) Copyright 2001 G.Richard Keech
   This code is released under the terms of the FSF GPL version 2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <mach/error.h>
#else
#include <error.h>
#endif
#define NTP_PORT  123

/* This program uses an NTP mode 6 control message, which is the
   same as that used by the ntpq command.  The ntpdc command uses
   NTP mode 7, details of which are elusive.
   For details on the format of NTP control message, see
   http://www.eecis.udel.edu/~mills/database/rfc/rfc1305/rfc1305b.ps.
   This document covers NTP version 2, however the control message
   format works with ntpd version 4 and earlier.
   Section 3.2 (pp 9 ff) of RFC1305b describes the data formats used by
   this program.

   This returns:
      0:   if clock is synchronised.
      1:   if clock is not synchronised.
      2:   if clock state cannot be determined, eg if
           ntpd is not contactable.                 */
/* ------------------------------------------------------------------------*/
/* value of Byte1 and Byte2 in the ntpmsg       */
#define B1VAL 0x16   /* VN = 2, Mode = 6 */
#define B2VAL 2      /* Response = 0; ( this makes the packet a command )
                        Error    = 0;
                        More     = 0;
                        Op Code  = 2 (read variables command) */
#define RMASK 0x80  /* bit mask for the response bit in Status Byte2 */
#define EMASK 0x40  /* bit mask for the error bit in Status Byte2 */
#define MMASK 0x20  /* bit mask for the more bit in Status Byte2 */
#define PAYLOADSIZE 468 /* size in bytes of the message payload string */
/*-------------------------------------------------------------------------*/

void die (char *msg) {
  fprintf (stderr,"%s\n",msg);
  exit (2);
}
/*-------------------------------------------------------------------------*/
int main (void) {
  int                   rc;        //  return code
  struct sockaddr_in    sock;
  struct in_addr        address;
  int                   sd;        /* file descriptor for socket */
  fd_set                fds;
  struct timeval        tv;
  int                   n;         /* number returned from select call */
  unsigned char         byte1ok;
  unsigned char         byte2ok;

  struct  {               /* RFC-1305 NTP control message format */
    unsigned char byte1;  /* Version Number: bits 3 - 5; Mode: bits 0 - 2; */
    unsigned char byte2;  /* Response: bit 7;
                             Error: bit 6;
                             More: bit 5;
                             Op code: bits 0 - 4 */
    unsigned short sequence;
    unsigned char  status1;  /* LI and clock source */
    unsigned char  status2;  /* count and code */
    unsigned short AssocID;
    unsigned short Offset;
    unsigned short Count;
    char payload[PAYLOADSIZE];
    char authenticator[96];
  } ntpmsg;

  char buff[PAYLOADSIZE];  /* temporary buffer holding payload string */

  unsigned int clksrc;
  char *clksrcname[] = {  /* Refer RFC-1305, Appendix B, Section 2.2.1 */
    "unspecified",    /* 0 */
    "atomic clock",   /* 1 */
    "VLF radio",      /* 2 */
    "HF radio",       /* 3 */
    "UHF radio",      /* 4 */
    "local net",      /* 5 */
    "NTP server",     /* 6 */
    "UDP/TIME",       /* 7 */
    "wristwatch",     /* 8 */
    "modem"};         /* 9 */
  char *newstr;
  char *dispstr;
  char *delaystr;
  const char DISP[] = "rootdisp=";
  const char DELAY[] = "rootdelay=";
  const char STRATUM[] = "stratum=";
  const char POLL[] = "tc=";
  const char REFID[] = "refid=";

  /* initialise timeout value */
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  /* initialise file descriptor set */
  FD_ZERO(&fds);

  inet_aton("127.0.0.1", &address);
  sock.sin_family = AF_INET;
  sock.sin_addr = address;
  sock.sin_port = htons(NTP_PORT);

  /*----------------------------------------------------------------*/
  /* Compose the command message */

  memset ( &ntpmsg, 0, sizeof(ntpmsg));
  ntpmsg.byte1 = B1VAL;
  ntpmsg.byte2 = B2VAL;  ntpmsg.sequence=htons(1);
  /*---------------------------------------------------------------------*/
  /* Send the command message */
  if ((sd = socket (PF_INET, SOCK_DGRAM, 0)) < 0)
    die("unable to open socket");

  if (connect(sd, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
    perror ("On connect");
    die("unable to connect to socket");
  }
  FD_SET(sd, &fds);

  if (send(sd, &ntpmsg, sizeof(ntpmsg), 0) < 0) {
    perror ("On send");
    die ("unable to send command to NTP port");
  }
  /*----------------------------------------------------------------------*/
  /* Receive the reply message */
  n = select (sd+1, &fds, (fd_set *)0, (fd_set *)0 , &tv);

  if (n == 0)
    die ("timeout");

  if (n == -1)
    die ("error on select");

  if ((n = recv (sd, &ntpmsg, sizeof(ntpmsg), 0)) < 0)
    die ("Unable to talk to NTP daemon. Is it running?");

  /*----------------------------------------------------------------------*/
  /* Interpret the received NTP control message */
  //printf("NTP mode 6 message\n");
  //printf("cmd = %0x%0x\n",ntpmsg.byte1,ntpmsg.byte2);
  //printf("status = %0x%0x\n",ntpmsg.status1,ntpmsg.status2);
  //printf("AssocID = %0x\n",ntpmsg.AssocID);
  //printf("Offset = %0x\n",ntpmsg.Offset);
  //printf("%s\n\n",ntpmsg.payload);
  /* For the reply message to be valid, the first byte should be as sent,
     and the second byte should be the same, with the response bit set */
  byte1ok = ((ntpmsg.byte1&0x3F) == B1VAL);
  byte2ok = ((ntpmsg.byte2 & ~MMASK) == (B2VAL|RMASK));
  if (!(byte1ok && byte2ok)) {
    fprintf (stderr,"status word is 0x%02x%02x\n", ntpmsg.byte1,ntpmsg.byte2 );
    die ("return data appears to be invalid based on status word");
  }

  if (!(ntpmsg.byte2 | EMASK)) {
    fprintf (stderr,"status byte2 is %02x\n", ntpmsg.byte2 );
    die ("error bit is set in reply");
  }

  if (!(ntpmsg.byte2 | MMASK)) {
    fprintf (stderr,"status byte2 is %02x\n", ntpmsg.byte2 );
    fprintf (stderr,"More bit unexpected in reply");
  }

  /* if the leap indicator (LI), which is the two most significant bits
     in status byte1, are both one, then the clock is not synchronised. */
  if ((ntpmsg.status1 >> 6) == 3) {
    printf ("unsynchronised\n");

    /* look at the system event code and see if indicates system restart */
    if ((ntpmsg.status2 & 0x0F) == 1)
      printf ("  time server re-starting\n");
      rc=1;
  }
  else {
    rc=0;
    printf ("synchronised to ");

    clksrc = (ntpmsg.status1 & 0x3F);
    if (clksrc < 10)
      printf("%s", clksrcname[clksrc]);
    else
      printf("unknown source");

    if (clksrc == 6) {
      // source of sync is another NTP server so check the IP address
      strncpy(buff, ntpmsg.payload, sizeof(buff));
      if ((newstr = strstr (buff, REFID))) {
	newstr += sizeof(REFID) - 1;
	dispstr = strtok(newstr,",");

	/* Check the resultant string is of a reasonable length */
	if ((strlen (dispstr) == 0) || (strlen (dispstr) > 16)) {
	  printf (" <IP unreadable>");
	}
	else {
	  printf(" (%s)",dispstr);
	}
      } else {
	rc=1;
	printf (" <IP unknown>");
      }

    }
    /* the message payload is an ascii string like
       version="ntpd 4.0.99k Thu Apr  5 14:21:47 EDT 2001 (1)",
       processor="i686", system="Linux2.4.2-2", leap=0, stratum=3,
       precision=-17, rootdelay=205.535, rootdispersion=57.997, peer=22924,
       refid=203.21.84.4, reftime=0xbedc2243.820c282c, poll=10,
       clock=0xbedc2310.75708249, state=4, phase=0.787, frequency=19.022,
       jitter=8.992, stability=0.029 */

    strncpy(buff, ntpmsg.payload, sizeof(buff));
    if ((newstr = strstr (buff, STRATUM))) {
      newstr += sizeof(STRATUM) - 1;
      dispstr = strtok(newstr,",");

      /* Check the resultant string is of a reasonable length */
      if ((strlen (dispstr) == 0) || (strlen (dispstr) > 2)) {
	printf (", stratum unreadable\n");
      }
      else {
	printf(" at stratum %s \n",dispstr);
      }
    } else {
      rc=1;
      printf (", stratum unknown\n");
    }

    /* Set the position of the start of the string to
       "rootdispersion=" part of the string. */
    strncpy(buff, ntpmsg.payload, sizeof(buff));
    if ((dispstr = strstr (buff, DISP)) && (delaystr = strstr (buff, DELAY))) {
      dispstr += sizeof(DISP) - 1;
      dispstr = strtok(dispstr,",");
      delaystr += sizeof(DELAY) - 1;
      delaystr = strtok(delaystr,",");

      /* Check the resultant string is of a reasonable length */
      if ((strlen (dispstr) == 0) || (strlen (dispstr) > 10) ||
	      (strlen (delaystr) == 0) || (strlen (delaystr) > 10)) {
	printf ("accuracy unreadable\n");
      }
      else {
	printf("   time correct to within %.0f ms\n", atof(dispstr) + atof(delaystr) / 2.0);
      }
    } else {
      rc=1;
      printf ("accuracy unknown\n");
    }
  }

  strncpy(buff, ntpmsg.payload, sizeof(buff));
  if ((newstr = strstr (buff, POLL))) {
    newstr += sizeof(POLL) - 1;
    dispstr = strtok(newstr,",");

    /* Check the resultant string is of a reasonable length */
    if ((strlen (dispstr) == 0) || (strlen (dispstr) > 2)) {
      printf ("poll interval unreadable\n");
    }
    else {
      printf("   polling server every %d s\n",1 << atoi(dispstr));
    }
  } else {
    rc=1;
    printf ("poll interval unknown\n");
  }

  return rc;
}
