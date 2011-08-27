/*
 *      Copyright (C) 2011 Jonathan Marshall
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>

#include "GUIShader.h"
#include "MatrixGLES.h"
 
#include <GLUT/glut.h>
#include <OpenGL/glext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>

bool showFPS = true;
GLint screenWidth, screenHeight;

GLuint texture = 0;
uint8_t *mask = NULL;
bool clear_stencil = true;
float *points = NULL;
size_t num_points = 7108;
float band_width = 10;

using namespace Shaders;

CGUIShader *shader = NULL;

void drawGLString(GLfloat x, GLfloat y, char *string)
{
  int len, i;

  glRasterPos2f(x, y);
  len = (int) strlen(string);
  for (i = 0; i < len; i++) {
    glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, string[i]);
  }
}

void drawInfo (GLint window_width, GLint window_height, float fps)
{
	char outString [256] = "";
	GLint vp[4];
	GLint lineSpacing = 13;
	GLint line = 0;
	GLint startOffest = 7;
	
	glGetIntegerv(GL_VIEWPORT, vp);
	glViewport(0, 0, window_width, window_height);

  g_matrices.MatrixMode(MM_PROJECTION);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();
  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();
  g_matrices.Scalef(2.0f / window_width, -2.0f / window_height, 1.0f);
	g_matrices.Translatef(-window_width / 2.0f, -window_height / 2.0f, 0.0f);

	// draw 
  glDisable(GL_TEXTURE_2D);
	glColor3f (1.0, 1.0, 1.0);
	if (showFPS)
  {
		sprintf (outString, "Frame rate: %0.1f", fps);
		drawGLString (10, window_height - (lineSpacing * line++) - startOffest, outString);
	}

  g_matrices.PopMatrix();
  g_matrices.MatrixMode(MM_PROJECTION);
  g_matrices.PopMatrix();
	
	glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void initShaders()
{
  shader = new CGUIShader("/Users/jmarshall/Massey/kde/real_time_kde/shaders/default.glsl");
  if (!shader->CompileAndLink())
  {
    shader->Free();
    delete shader;
    shader = NULL;
  }
}

/*
void CRenderSystemGLES::DisableGUIShader()
{
  if (m_pGUIshader[m_method])
  {
    m_pGUIshader[m_method]->Disable();
  }
  m_method = SM_DEFAULT;
}

GLint CRenderSystemGLES::GUIShaderGetPos()
{
  if (m_pGUIshader[m_method])
    return m_pGUIshader[m_method]->GetPosLoc();
  
  return -1;
}

GLint CRenderSystemGLES::GUIShaderGetCol()
{
  if (m_pGUIshader[m_method])
    return m_pGUIshader[m_method]->GetColLoc();
  
  return -1;
}

GLint CRenderSystemGLES::GUIShaderGetCoord0()
{
  if (m_pGUIshader[m_method])
    return m_pGUIshader[m_method]->GetCord0Loc();
  
  return -1;
}

GLint CRenderSystemGLES::GUIShaderGetCoord1()
{
  if (m_pGUIshader[m_method])
    return m_pGUIshader[m_method]->GetCord1Loc();
  
  return -1;
}
 */

void buildKernelTexture(float bw)
{
  int size = 64;

  if (glIsTexture(texture))
    glDeleteTextures(1, &texture);

  glGenTextures(1, &texture);
  glBindTexture( GL_TEXTURE_2D, texture );
  glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

  // when texture area is small, bilinear filter
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

  // the texture wraps over at the edges (repeat)
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );

  float *kernel = new float[size * size * sizeof(float)];
  float total = 0;
  float s2 = (size * size) / 36.0f;
  float scale = size*size*bw/10;
  for (int y = 0; y < size; y++)
  {
    for (int x = 0; x < size; x++)
    { // compute gaussian kernel
      float value = 1/(2*M_PI*s2) * exp(-0.5*((x-0.5*size)*(x-0.5*size) + (y - 0.5*size)*(y - 0.5*size)) / s2);
      kernel[y * size + x] = scale * value / (bw * bw);
      total += value;
    }
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA16, size, size, 0, GL_ALPHA, GL_FLOAT, kernel );
  delete[] kernel;
}

void readMask()
{
  FILE *file = fopen("/Users/jmarshall/Massey/kde/real_time_kde/data/mask.txt", "r");
  
  if (file)
  {
    mask = new uint8_t[512*512];
    uint8_t *p = mask;
    while (!feof(file) && p < mask + 512*512)
    {
      char c = fgetc(file);
      if (c == '1' || c == '0')
        *p++ = (c == '1') ? 0 : 0xff;
    }
    fclose(file);
  }
}

void readData()
{
  FILE *file = fopen("/Users/jmarshall/Massey/kde/real_time_kde/data/points.txt", "r");
  
  if (file)
  {
    points = new float[2*num_points];
    float *p = points;
    while (!feof(file) && p < points + 2*num_points)
    {
      fscanf(file, "%f %f", p, p+1);
      p += 2;
    }
    fclose(file);
  }
}

void init (void)
{
	glDisable(GL_DEPTH_TEST);

	glShadeModel(GL_SMOOTH);    
	glFrontFace(GL_CCW);

	glColor3f(1.0,1.0,1.0);

  buildKernelTexture(band_width);
  readMask();
  readData();
  initShaders();

	glClearColor(0.0,0.0,0.0,0.0);         /* Background recColor */
	
	glPolygonOffset (1.0, 1.0);
  
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  if (mask)
  {
    glEnable(GL_STENCIL_TEST);
    glStencilFunc (GL_NOTEQUAL, 0x1, 0x1);
  }
}

void reshape (int w, int h)
{
	glViewport(0,0,(GLsizei)w,(GLsizei)h);

	screenWidth = w;
	screenHeight = h;

  clear_stencil = true;

	glutPostRedisplay();
}


struct SVertex
{
  float x, y, z;
  unsigned char r, g, b, a;
  float u, v;
};

SVertex *vertices = NULL;

int lastTime = 0;
void maindisplay(void)
{
  g_matrices.MatrixMode(MM_PROJECTION);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();
  g_matrices.Ortho(0, 512, 0, 512, -1, 1);

  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PushMatrix();
  g_matrices.LoadIdentity();

	int nowTime = glutGet(GLUT_ELAPSED_TIME);
  float fps = 1000.0f / (nowTime - lastTime);
  lastTime = nowTime;

	glClearColor (0, 0, 0, 0);//1, 1, 1, 1.0);	// clear the surface
	glClear (GL_COLOR_BUFFER_BIT);

  if (mask && clear_stencil)
  {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 512, 0, 512, -1, 1);
    glClearStencil(1);
    glClear(GL_STENCIL_BUFFER_BIT);
    glRasterPos2f(0, 0);
    glDrawPixels(512, 512, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, mask);
    clear_stencil = false;
  }

  glEnable(GL_STENCIL_TEST);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  if (shader && points)
  {
    shader->Enable();
    GLint posLoc  = shader->GetPosLoc();
    GLint colLoc  = shader->GetColLoc();
    GLint tex0Loc = shader->GetCord0Loc();

    if (!vertices)
      vertices = new SVertex[4*num_points];

    float h = 3*band_width;
    for (size_t i = 0; i < num_points; i++)
    {
      // sample a random point i in 0..100,0..100
      int index = ((float)rand() * num_points) / RAND_MAX;
      float x = points[2*index];//rand() * 512.0f / RAND_MAX;
      float y = points[2*index+1];//rand() * 512.0f / RAND_MAX;

      vertices[4*i+0].x = x - h; vertices[4*i+0].y = y - h;
      vertices[4*i+1].x = x + h; vertices[4*i+1].y = y - h;
      vertices[4*i+2].x = x + h; vertices[4*i+2].y = y + h;
      vertices[4*i+3].x = x - h; vertices[4*i+3].y = y + h;
      
      vertices[4*i+0].u = 0; vertices[4*i+0].v = 0;
      vertices[4*i+1].u = 1; vertices[4*i+1].v = 0;
      vertices[4*i+2].u = 1; vertices[4*i+2].v = 1;
      vertices[4*i+3].u = 0; vertices[4*i+3].v = 1;

      for (int j = 0; j < 3; j++)
      {
        vertices[4*i+j].r = 0;
        vertices[4*i+j].g = 128;
        vertices[4*i+j].b = 255;
        vertices[4*i+j].a = 255;        
      }
    }

    glVertexAttribPointer(posLoc,  3, GL_FLOAT,         GL_FALSE, sizeof(SVertex), (char*)vertices + offsetof(SVertex, x));
    // Normalize color values. Does not affect Performance at all.
    glVertexAttribPointer(colLoc,  4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SVertex), (char*)vertices + offsetof(SVertex, r));
    glVertexAttribPointer(tex0Loc, 2, GL_FLOAT,         GL_FALSE, sizeof(SVertex), (char*)vertices + offsetof(SVertex, u));

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(colLoc);
    glEnableVertexAttribArray(tex0Loc);

    glDrawArrays(GL_QUADS, 0, num_points);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(colLoc);
    glDisableVertexAttribArray(tex0Loc);
    
    shader->Disable();
  }
  glDisable(GL_STENCIL_TEST);

	drawInfo (screenWidth, screenHeight, fps);

	glutSwapBuffers();
}

void special(int key, int px, int py)
{
  switch (key) {
	case GLUT_KEY_UP: // arrow forward, close in on world
		break;
	case GLUT_KEY_DOWN: // arrow back, back away from world
		break;
	case GLUT_KEY_LEFT: // arrow left, smaller aperture
    if (band_width > 1)
    {
      band_width -= 1;
      buildKernelTexture(band_width);
      glutPostRedisplay();
    }
		break;
	case GLUT_KEY_RIGHT: // arrow right, larger aperture
    if (band_width < 40)
    {
      band_width += 1;
      buildKernelTexture(band_width);
      glutPostRedisplay();
    }
		break;
  }
}

void mouseMove(int x, int y)
{
  glutPostRedisplay();
}

void key(unsigned char inkey, int px, int py)
{
  switch (inkey) {
	case 27:
		exit(0);
		break;
	case 'f': // fps
	case 'F':
    showFPS = !showFPS;
		glutPostRedisplay();
		break;
  }
}

void onidle()
{
  // force redraw?
 // glutPostRedisplay();
}

int main (int argc, const char * argv[])
{
  glutInit(&argc, (char **)argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_STENCIL); // non-stereo for main window
	glutInitWindowPosition (300, 50);
	glutInitWindowSize (512, 512);
	glutCreateWindow("Real time kernel density estimation");

  init(); // standard GL init
  
  glutReshapeFunc (reshape);
  glutDisplayFunc (maindisplay);
	glutKeyboardFunc (key);
	glutSpecialFunc (special);
  glutMotionFunc(mouseMove);
  glutIdleFunc(onidle);
  glutMainLoop();
  
  delete[] mask;
  return 0;
}





