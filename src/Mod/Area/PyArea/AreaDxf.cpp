// AreaDxf.cpp
// Copyright (c) 2011, Dan Heeks
// This program is released under the BSD license. See the file COPYING for details.

#include "AreaDxf.h"
#include <libarea/Area.h>

AreaDxfRead::AreaDxfRead(CArea* area, const char* filepath):CDxfRead(filepath), m_area(area){}

void AreaDxfRead::StartCurveIfNecessary(const Base::Vector3d& s)
{
	Point ps(s.x, s.y);
	if((m_area->m_curves.size() == 0) || (m_area->m_curves.back().m_vertices.size() == 0) || (m_area->m_curves.back().m_vertices.back().m_p != ps))
	{
		// start a new curve
		m_area->m_curves.emplace_back();
		m_area->m_curves.back().m_vertices.push_back(ps);
	}
}

void AreaDxfRead::OnReadLine(const Base::Vector3d& s, const Base::Vector3d& e, bool /*hidden*/)
{
	StartCurveIfNecessary(s);
	m_area->m_curves.back().m_vertices.push_back(Point(e.x, e.y));
}

void AreaDxfRead::OnReadArc(const Base::Vector3d& s, const Base::Vector3d& e, const Base::Vector3d& c,
                            bool dir, bool /*hidden*/)
{
	StartCurveIfNecessary(s);
	m_area->m_curves.back().m_vertices.emplace_back(dir?1:0, Point(e.x, e.y), Point(c.x, c.y));
}
