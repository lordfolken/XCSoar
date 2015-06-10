/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2015 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Screen/Icon.hpp"
#include "Screen/Layout.hpp"
#include "Screen/Canvas.hpp"

#ifdef ENABLE_OPENGL
#include "Screen/OpenGL/Texture.hpp"
#include "Screen/OpenGL/Scope.hpp"

#ifdef USE_GLSL
#include "Screen/OpenGL/Shaders.hpp"
#include "Screen/OpenGL/Program.hpp"
#else
#include "Screen/OpenGL/Compatibility.hpp"
#endif
#endif

void
MaskedIcon::LoadResource(ResourceId id, ResourceId big_id, bool center)
{
  if (Layout::ScaleEnabled()) {
    if (big_id.IsDefined())
      bitmap.Load(big_id);
    else
      bitmap.LoadStretch(id, Layout::FastScale(1));
  } else
    bitmap.Load(id);

  assert(IsDefined());

  size = bitmap.GetSize();
#ifndef ENABLE_OPENGL
  /* left half is mask, right half is icon */
  size.cx /= 2;
#endif

  if (center) {
    origin.x = size.cx / 2;
    origin.y = size.cy / 2;
  } else {
    origin.x = 0;
    origin.y = 0;
  }
}

void
MaskedIcon::Draw(Canvas &canvas, PixelScalar x, PixelScalar y) const
{
  assert(IsDefined());

#ifdef ENABLE_OPENGL
#ifdef USE_GLSL
  OpenGL::texture_shader->Use();
#else
  const GLEnable<GL_TEXTURE_2D> scope;
  OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif

  const GLBlend blend(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLTexture &texture = *bitmap.GetNative();
  texture.Bind();
  texture.Draw(x - origin.x, y - origin.y);
#else

#ifdef USE_GDI
  /* our icons are monochrome bitmaps, and GDI uses current colors of
     the destination HDC when blitting from a monochrome HDC */
  canvas.SetTextColor(COLOR_BLACK);
  canvas.SetBackgroundColor(COLOR_WHITE);
#endif

  canvas.CopyOr(x - origin.x, y - origin.y, size.cx, size.cy,
                 bitmap, 0, 0);
  canvas.CopyAnd(x - origin.x, y - origin.y, size.cx, size.cy,
                  bitmap, size.cx, 0);
#endif
}

void
MaskedIcon::Draw(Canvas &canvas, const PixelRect &rc, bool inverse) const
{
  const int offsetx = (rc.right - rc.left - size.cx) / 2;
  const int offsety = (rc.bottom - rc.top - size.cy) / 2;

#ifdef ENABLE_OPENGL
#ifdef USE_GLSL
  if (inverse)
    OpenGL::invert_shader->Use();
  else
    OpenGL::texture_shader->Use();
#else
  const GLEnable<GL_TEXTURE_2D> scope;

  if (inverse) {
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

    /* invert the texture color */
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);

    /* copy the texture alpha */
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
  } else
    /* simple copy */
    OpenGL::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif

  const GLBlend blend(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLTexture &texture = *bitmap.GetNative();
  texture.Bind();
  texture.Draw(rc.left + offsetx, rc.top + offsety);

#else
  if (inverse) // black background
    canvas.CopyNotOr(rc.left + offsetx, rc.top + offsety, size.cx, size.cy,
                     bitmap, size.cx, 0);

  else
    canvas.CopyAnd(rc.left + offsetx, rc.top + offsety, size.cx, size.cy,
                   bitmap, size.cx, 0);
#endif

}
