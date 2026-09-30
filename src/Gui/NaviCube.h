/***************************************************************************
 *   Copyright (c) 2017 Kustaa Nyholm  <kustaa.nyholm@sparetimelabs.com>   *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef SRC_GUI_NAVICUBE_H_
#define SRC_GUI_NAVICUBE_H_

#include <functional>

#include <CXX/Extensions.hxx>

#include "Renderer/Renderer.h"

class SoEvent;
class SoSeparator;
class QWidget;

namespace Gui {
class View3DInventorViewer;
}

class NaviCubeImplementation;

class GuiExport NaviCube {
public:
	enum Corner {
		TopLeftCorner,
		TopRightCorner,
		BottomLeftCorner,
		BottomRightCorner
	};
	/// A null viewer makes a served cube: no viewer, no GL context, no
	/// events -- only the overlay graphs, placed by setPosition(), for a
	/// publisher with no 3D view (SceneServeSource) to state.
	NaviCube(Gui::View3DInventorViewer* viewer) ;
	virtual ~NaviCube();
	void drawNaviCube();
	bool processSoEvent(const SoEvent* ev);
	void setCorner(Corner);
	/// The corner nearest the cube's position, for whatever keeps out
	/// of its way (docked overlay panels, the share pill).
	Corner getCorner() const;
	/// Where the cube sits, each 0..1: the fraction of the room the view
	/// leaves it, x from the left edge, y from the top (the view
	/// properties NaviCubeX/NaviCubeY). A corner is 0 or 1 on both.
	void setPosition(float x, float y);
	void getPosition(float &x, float &y) const;
	/// The position a corner stands for.
	static void cornerPosition(Corner, float &x, float &y);
	/// State a cube of \a size pixels at position (\a x, \a y) on an
	/// overlay anchor: the placement alone, which is all a consumer of
	/// any viewport size needs (OverlayAnchor::cornerRect).
	static void fillPlacement(Render::OverlayAnchor &anchor, float x, float y, int size);
	/// Called when a served cube's graphs need stating again (its
	/// preferences changed, or it moved); a viewer's cube redraws instead.
	void setChangedCallback(std::function<void()>);
	/// Coin overlay twins of drawNaviCube() for the external render
	/// backend (raw-GL overlay Coin-ification, phase C): the rotating
	/// cube under a corner mini-perspective anchor, and the viewport-
	/// fixed rotate buttons / menu icon under a corner ortho anchor.
	/// Null when hidden (auto-hide) or before the GL textures exist.
	/// The cube stays clickable through the existing GL pick pass.
	SoSeparator *getOverlayCubeGraph(Render::OverlayAnchor &anchor);
	SoSeparator *getOverlayButtonGraph(Render::OverlayAnchor &anchor);
    static int getNaviCubeSize();
    static void setColors(QWidget *parent);
    static void setLabels(QWidget *parent);
private:
	NaviCubeImplementation* m_NaviCubeImplementation;
};

#endif /* SRC_GUI_NAVICUBE_H_ */
