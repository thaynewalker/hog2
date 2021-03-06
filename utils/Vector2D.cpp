/**
*
*  Modified by Thayne Walker 2017.
*
* This file is part of HOG2.
*
* HOG2 is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* HOG2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with HOG2; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "Vector2D.h"
#include "GridStates.h"

std::ostream& operator <<(std::ostream & out, Vector2D const& v)
{
  out << "(" << v.x << ", " << v.y << ")";
  return out;
}

Vector2D::Vector2D(xyLoc const& v):Vector2D(v.x,v.y){}
