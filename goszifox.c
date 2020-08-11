/* gozsifox - ozsiFox oscilloscope display
   Copyright (C) 2003 James Bowman

gozsifox is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This software is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this software; see the file COPYING.  If not, write to the
Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */



/* Modificat per Jordi Orts (http://www.jorts.net), gener 2012

   * codi de sincronisme canviat a 0x5C, compatible amb versió
     high-tech-diesel de Oszifox

   * Afegit moviment horitzontal i vertical del traç per mesurar
     4=left 6=right 8=up 2=down 5=center */


#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include <GL/glut.h>

#define DEFAULT_SERIAL_DEVICE     "/dev/ttyS0"

static struct {
  int fd;                       /* Serial port */
  int si;                       /* Current offset into datablk. -1
                                   means waiting for sync */
  unsigned char datablk[200];   /* Data from scope */
  int gotdata;                  /* flag: true if datablk is full */
  int deltax;
  int deltay;
} state;

/************************************************************************/

static void serial_open(char *serial_device)
{
  state.fd = open(serial_device, O_RDWR | O_NONBLOCK);

  if (state.fd < 0) {
    perror(serial_device);
    exit(1);
  }

  {
    struct termios sb;
    int s;

    s = tcgetattr(state.fd, &sb);
    if (s < 0) {
      fprintf(stderr, "Failed to open serial device %s", serial_device);
      exit(1);
    }

    sb.c_iflag = IGNPAR;
    sb.c_oflag = 0;
    sb.c_lflag = 0;
    sb.c_cflag = (CLOCAL | CS7 | CREAD);
    for (s = 0; s < NCCS; s++)
      sb.c_cc[s] = 0;
    sb.c_cc[VMIN] = 1;
    sb.c_cc[VTIME] = 0;

    cfsetspeed(&sb, B19200);
    if (tcsetattr(state.fd, TCSANOW, &sb) < 0) {
      perror("tcsetattr");
      exit(1);
    }
  }
}

/************************************************************************/

int W, H;
int paused;

typedef enum { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER } alignment;

static void
showMessage(alignment al, GLfloat x, GLfloat y, GLfloat size, char *message)
{
  float scale = .0004 * size;
  float width;
  char *pc;

  width = 0.0;
  for (pc = message; *pc; pc++) {
    width += glutStrokeWidth(GLUT_STROKE_ROMAN, *pc);
  }

  glPushMatrix();
  switch (al) {
  case ALIGN_LEFT:
    glTranslatef(x, y, 0);
    break;
  case ALIGN_CENTER:
    glTranslatef(x - (0.5 * width * scale), y, 0);
    break;
  case ALIGN_RIGHT:
    glTranslatef(x - (width *scale), y, 0);
    break;
  default:
    assert(0);
  }

  glScalef(scale, scale, 1);
  for (pc = message ; *pc; pc++) {
    glutStrokeCharacter(GLUT_STROKE_ROMAN, *pc);
  }
  glPopMatrix();
}

#define B_SPLINE_SUPPORT 2
static double
B_spline_filter(double t)
{
  double tt, r;

  t = fabs(t);
  if (t < 1.0) {
    tt = t * t;
    r = ((0.5 * tt * t) - tt + (2.0 / 3.0));
  } else if (t < 2.0) {
    t = 2.0 - t;
    r = ((1.0 / 6.0) * (t * t * t));
  } else {
    r = 0.0;
  }

  return r;
}

static double sinc(double x)
{
  if (x != 0.0) 
    return sin(M_PI * x) / (M_PI * x);
  else
    return 1.0;
}

#define LANCZOS3_SUPPORT        3

static double
Lanczos3_filter(double t)
{
  t = fabs(t);
  if (t < 3.0) 
    return(sinc(t) * sinc(t / 3.0));
  else
    return(0.0);
}

#define filter(x) B_spline_filter(x)
#define FILTER_SUPPORT B_SPLINE_SUPPORT
//#define filter(x) Lanczos3_filter(x)
//#define FILTER_SUPPORT LANCZOS3_SUPPORT

#define DATABLK_FIRST_SAMPLE 4
#define DATABLK_N_SAMPLES 128

typedef enum { 
  COLOR_GRATICULE_0, 
  COLOR_GRATICULE_1, 
  COLOR_GRATICULE_2,
  COLOR_TEXT,
  COLOR_SIGNAL
} color;

static void
setColor(color c)
{
  switch (c) {
  case COLOR_GRATICULE_0:
    glColor3f(0.3, 0.3, 0.0);
    break;
  case COLOR_GRATICULE_1:
    glColor3f(0.7, 0.7, 0.0);
    break;
  case COLOR_GRATICULE_2:
    glColor3f(0.8, 0.8, 0.0);
    break;
  case COLOR_SIGNAL:
    glColor3f(0.5, 1.0, 0.6);
    break;
  case COLOR_TEXT:
    glColor3f(1, 1, 1);
    break;
  }
}

#define DATA_Y_MIN 0.1
#define DATA_Y_MAX 0.9

static void
redraw_viewing(void)
{
  int i, j;
  double timebase;

  glClearColor(0, 0.1, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  /* Set state for drawing smooth lines in a CRT-scope style */

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glEnable(GL_BLEND);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glLineWidth(2.0);

  if (!state.gotdata) {
    setColor(COLOR_TEXT);
    showMessage(ALIGN_CENTER, 0.5, 0.5, 1.0, "WAITING FOR DATA");
  } else {
    /* What's the timebase? */
    switch (state.datablk[1]) {
    case 0:
      timebase = 50.0e-9;
      break;
    case 1:
      timebase = 100.0e-9;
      break;
    case 2:
      timebase = 0.5e-6;
      break;
    case 3:
      timebase = 1.0e-6;
      break;
    case 4:
      timebase = 5.0e-6;
      break;
    case 5:
      timebase = 10e-6;
      break;
    case 6:
      timebase = 50e-6;
      break;
    case 7:
      timebase = 0.1e-3;
      break;
    case 8:
      timebase = 0.5e-3;
      break;
    case 9:
      timebase = 1.0e-3;
      break;
    default:
      timebase = 1.0e-3;
      break;
    }

    /* Horizontal lines */
    glBegin(GL_LINES);
    for (i = 0; i <= 10; i++) {
      if (i == 5)
        setColor(COLOR_GRATICULE_2);
      else
        setColor(COLOR_GRATICULE_0);
      glVertex2f(0, DATA_Y_MIN + (DATA_Y_MAX - DATA_Y_MIN) * (i / 10.0));
      glVertex2f(1, DATA_Y_MIN + (DATA_Y_MAX - DATA_Y_MIN) * (i / 10.0));
    }
    glEnd();

    setColor(COLOR_TEXT);
    {
      float t;
      float unit;
      char *unitname;
      char label[10];
      int graticule_spacing;

      /* Select a unit for display */
      t = timebase * 99;
      if (t < 1.0e-6) {
        unitname = "ns";
        unit = 1.0e-9;
      } else if (t < 1.0e-3) {
        unitname = "us";
        unit = 1.0e-6;
      } else {
        unitname = "ms";
        unit = 1.0e-3;
      } 
      t = 50.0 * timebase / unit;
      if (((int)(10.0 * t + 0.5) % 10) == 0) {
        graticule_spacing = 50;
      } else {
        graticule_spacing = 40;
      }

      glBegin(GL_LINES);
      for (i = 0; i < 128; i++) {
        if ((i % graticule_spacing) == 0) {
          setColor(COLOR_GRATICULE_1);
          glVertex2f(i / 128.0, DATA_Y_MIN);
          glVertex2f(i / 128.0, DATA_Y_MAX);
        } else if ((i % 10) == 0) {
          setColor(COLOR_GRATICULE_0);
          glVertex2f(i / 128.0, DATA_Y_MIN);
          glVertex2f(i / 128.0, DATA_Y_MAX);
        }
      }
      glEnd();

      setColor(COLOR_TEXT);
      glLineWidth(3.0);
      showMessage(ALIGN_LEFT, 0.0, 0.02, 1.0, "0");
      for (i = 1; i < 128; i++) {
        if ((i % graticule_spacing) == 0) {
          t = timebase * i;
          sprintf(label,
                  "%.0f%s",
                  t / unit,
                  unitname);
          showMessage(ALIGN_CENTER, i / 128.0, 0.02, 1.0, label);
        }
      }

    }

    setColor(COLOR_SIGNAL);
    glLineWidth(4.0);

#define OVERSAMPLE 5

    {
      float l;
      float samples[DATABLK_N_SAMPLES];

      /* Copy 'raw' samples into samples[] array */
      for (i = 0; i < DATABLK_N_SAMPLES; i++) {
        samples[i] = state.datablk[DATABLK_FIRST_SAMPLE + i] / 64.0;
      }

      glBegin(GL_LINE_STRIP);
      for (i = -OVERSAMPLE; i < OVERSAMPLE * (DATABLK_N_SAMPLES + 1); i++) {
        int jb = i / OVERSAMPLE;
        l = 0.0;                  /* filtered level */
        for (j = jb - FILTER_SUPPORT; j <= jb + FILTER_SUPPORT; j++) {
          float xx =              /* distance from sample */
            ((float)i / OVERSAMPLE) - (float)j;
          float ff = filter(xx);  /* filter value */
          float ss;               /* sample value */

          /* Read sample */
          if (j <= 0) {
            ss = samples[0];
          } else if (j < DATABLK_N_SAMPLES) {
            ss = samples[0 + j];
          } else {
            ss = samples[DATABLK_N_SAMPLES - 1];
          }
          l += ss * ff;           /* accumulate */
        }
        glVertex2f(((i+state.deltax) / (float)(OVERSAMPLE * DATABLK_N_SAMPLES)), 
                   DATA_Y_MIN + state.deltay*0.01 + (DATA_Y_MAX - DATA_Y_MIN) * l);
      }
      glEnd();
    }

    {
      char label[80];
      static const char *coupling[4] = { "GND", "AC", "DC" };
      static const char *range[4] = { "1", "10", "100" };
    
      setColor(COLOR_TEXT);
      glLineWidth(2.0);

      if (paused) {
        showMessage(ALIGN_LEFT, 0.0, 0.94, 0.7, "PAUSED");
      } else {
        strcpy(label, "TRIG: ");
        if (state.datablk[2] & (1 << 6))
          strcat(label, "+INTERN");
        else if (state.datablk[2] & (1 << 5))
          strcat(label, "-INTERN");
        else if (state.datablk[2] & (1 << 4))
          strcat(label, "+EXTERN");
        else if (state.datablk[2] & (1 << 3))
          strcat(label, "-EXTERN");
        else 
          strcat(label, "AUTO");
        showMessage(ALIGN_LEFT, 0.0, 0.94, 0.7, label);
      }

      sprintf(label, "COUPLING: %s", coupling[3 & (state.datablk[0] >> 4)]);
      showMessage(ALIGN_RIGHT, 0.97, 0.94, 0.7, label);

      sprintf(label, "RANGE: %sV", range[3 & (state.datablk[0] >> 2)]);
      showMessage(ALIGN_CENTER, 0.5, 0.94, 0.7, label);


    }
  }

  glutSwapBuffers();
}

static void
reshape_viewing(int w, int h)
{
  glViewport(0, 0, w, h);
  W = w;
  H = h;
}

static void
animate(void)
{
  {
    unsigned char buf;

    if (paused)
	return;

    if (read(state.fd,  &buf, 1) == 1) {
      if ((buf & 0x5C) == 0x5C) { /* sync */
        state.si = 0;
      } else if (state.si != -1) {
        state.datablk[state.si++] = buf;
        if (state.si == 137) {
          /* Got a full buffer: display it */
          if (!paused)
            glutPostRedisplay();
          state.gotdata = 1;
          state.si = -1;
        }
      }
    }
  }
}

static void
key(unsigned char key, int x, int y)
{
  switch (key) {
  case 27:
  case 'q':
    exit(0);
  case ' ':
    paused = !paused;
    glutPostRedisplay();
    break;
  case '6':
    state.deltax++;
    glutPostRedisplay();
    break;
  case '4':
    state.deltax--;
    glutPostRedisplay();
    break;
  case '8':
    state.deltay++;
    glutPostRedisplay();
    break;
  case '2':
    state.deltay--;
    glutPostRedisplay();
    break;
  case '5':
    state.deltax=0;
    state.deltay=0;
    glutPostRedisplay();
    break;
  default:
//    paused = 0;
    break;
  }
}

int main(int argc, char *argv[])
{
  int ch;
  char *serial_device;
  
  glutInit(&argc, argv);

  serial_device = DEFAULT_SERIAL_DEVICE;
  while ((ch = getopt(argc, argv, "p:")) != -1) {
    switch (ch) {
    case 'p':
      serial_device = optarg;
      break;
    case '?':
    default:
      fprintf(stderr, "usage: goszifox [ -p serial_device ]\n");
      exit(1);
    }
  }

  serial_open(serial_device);
  state.si = -1;

  state.deltax=0;
  state.deltay=0;

  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
  glutInitWindowSize(1024, 768);

  glutCreateWindow("gozsifox");  
  glutReshapeFunc(reshape_viewing);
  glutDisplayFunc(redraw_viewing);

  glutIdleFunc(animate);
  glutKeyboardFunc(key);
  glutSetCursor(GLUT_CURSOR_NONE);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);

  /* Make the screen (0,0) to (1,1) */
  glMatrixMode(GL_MODELVIEW);
  glScalef(2.0f, 2.0f, 1.0f);
  glTranslatef(-0.5f, -0.5f, 0.0f);

  glutMainLoop();
  return 0;
}
