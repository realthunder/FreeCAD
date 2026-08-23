// AreaDxf.h
// Copyright (c) 2011, Dan Heeks
// This program is released under the BSD license. See the file COPYING for details.

#pragma once

#include "dxf.h"

class CSketch;
class CArea;
class CCurve;

class AreaDxfRead : public CDxfRead{
	void StartCurveIfNecessary(const Base::Vector3d& s);

public:
	CArea* m_area;
	AreaDxfRead(CArea* area, const char* filepath);

	// AreaDxfRead's virtual functions
	void OnReadLine(const Base::Vector3d& s, const Base::Vector3d& e, bool /*hidden*/) override;
	void OnReadArc(const Base::Vector3d& s, const Base::Vector3d& e, const Base::Vector3d& c,
	               bool dir, bool /*hidden*/) override;
};
