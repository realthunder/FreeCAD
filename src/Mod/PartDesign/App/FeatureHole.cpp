/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#include "PreCompiled.h"
#ifndef _PreComp_
# include <gp_Dir.hxx>
# include <BRep_Builder.hxx>
# include <BRepAlgoAPI_Cut.hxx>
# include <BRepAlgoAPI_Fuse.hxx>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepBuilderAPI_MakeSolid.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <BRepBuilderAPI_Sewing.hxx>
# include <BRepBuilderAPI_Transform.hxx>
# include <BRepClass3d_SolidClassifier.hxx>
# include <BRepOffsetAPI_MakePipeShell.hxx>
# include <BRepPrimAPI_MakeRevol.hxx>
# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <gp_Cylinder.hxx>
# include <Geom_Circle.hxx>
# include <GC_MakeArcOfCircle.hxx>
# include <Geom_TrimmedCurve.hxx>
# include <Standard_Version.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Wire.hxx>
# include <TopExp.hxx>
#endif


#include <Base/Placement.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <App/Document.h>
#include <App/Application.h>
#include <App/DocumentObject.h>
#include <Mod/Part/App/FaceMakerCheese.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include "FeatureHole.h"
#include "json.hpp"

FC_LOG_LEVEL_INIT("PartDesign", true, true);

namespace PartDesign {

/* TRANSLATOR PartDesign::Hole */

const char* Hole::DepthTypeEnums[]                   = { "Dimension", "ThroughAll", /*, "UpToFirst", */ nullptr };
const char* Hole::ThreadDepthTypeEnums[]             = { "Hole Depth", "Dimension", "Tapped (DIN76)",  nullptr };
const char* Hole::ThreadTypeEnums[]                  = { "None", "ISOMetricProfile", "ISOMetricFineProfile", "UNC", "UNF", "UNEF", "NPT", "BSP", "BSW", "BSF", "ISOTyre", nullptr};
// The fits by index: 0 the middle one, 1 the closest, 2 the widest; the
// names are each standard's own (ISO 273, ASME B18.2.8), upstream 70007a28c1
const char* Hole::ClearanceNoneEnums[]               = { "-", "-", "-", nullptr};
const char* Hole::ClearanceMetricEnums[]             = { "Medium", "Fine", "Coarse", nullptr};
const char* Hole::ClearanceUTSEnums[]                = { "Normal", "Close", "Loose", nullptr };
const char* Hole::ClearanceOtherEnums[]              = { "Normal", "Close", "Wide", nullptr };
const char* Hole::DrillPointEnums[]                  = { "Flat", "Angled", nullptr};

/* "None" profile */

const char* Hole::HoleCutType_None_Enums[]           = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr };
const char* Hole::ThreadClass_None_Enums[]           = { "None", nullptr };

/* Sources:
  http://www.engineeringtoolbox.com/metric-threads-d_777.html
  http://www.metalmart.com/tools/miscellaneous-guides/standard-drill-size/

*/

const Hole::ThreadDescription Hole::threadDescription[][171] =
{
    /* None */
    {
        { "---", 	6.0, 0.0, 0.0 },
    },

    /* ISO metric regular */
    /* ISO metric threaded core hole diameters according to ISO 2306 */
    // {name, thread diameter, thread pitch, core hole diameter}
    {
        { "M1", 	1.0,    0.25,   0.75 },
        { "M1.1", 	1.1,    0.25,   0.85 },
        { "M1.2", 	1.2,    0.25,   0.95 },
        { "M1.4", 	1.4,    0.30,   1.10 },
        { "M1.6", 	1.6,    0.35,   1.25 },
        { "M1.8", 	1.8,    0.35,   1.45 },
        { "M2", 	2.0,    0.40,   1.60 },
        { "M2.2", 	2.2,    0.45,   1.75 },
        { "M2.5", 	2.5,    0.45,   2.05 },
        { "M3", 	3.0,    0.50,   2.50 },
        { "M3.5", 	3.5,    0.60,   2.90 },
        { "M4", 	4.0,    0.70,   3.30 },
        { "M4.5", 	4.5,    0.75,   3.70 },
        { "M5", 	5.0,    0.80,   4.20 },
        { "M6", 	6.0,    1.00,   5.00 },
        { "M7", 	7.0,    1.00,   6.00 },
        { "M8", 	8.0,    1.25,   6.80 },
        { "M9", 	9.0,    1.25,   7.80 },
        { "M10", 	10.0,   1.50,   8.50 },
        { "M11", 	11.0,   1.50,   9.50 },
        { "M12", 	12.0,   1.75,   10.20 },
        { "M14", 	14.0,   2.00,   12.00 },
        { "M16", 	16.0,   2.00,   14.00 },
        { "M18", 	18.0,   2.50,   15.50 },
        { "M20", 	20.0,   2.50,   17.50 },
        { "M22", 	22.0,   2.50,   19.50 },
        { "M24", 	24.0,   3.00,   21.00 },
        { "M27", 	27.0,   3.00,   24.00 },
        { "M30", 	30.0,   3.50,   26.50 },
        { "M33", 	33.0,   3.50,   29.50 },
        { "M36", 	36.0,   4.00,   32.00 },
        { "M39", 	39.0,   4.00,   35.00 },
        { "M42", 	42.0,   4.50,   37.50 },
        { "M45", 	45.0,   4.50,   40.50 },
        { "M48", 	48.0,   5.00,   43.00 },
        { "M52", 	52.0,   5.00,   47.00 },
        { "M56", 	56.0,   5.50,   50.50 },
        { "M60", 	60.0,   5.50,   54.50 },
        { "M64", 	64.0,   6.00,   58.00 },
        { "M68", 	68.0,   6.00,   62.00 },
     },
    /* ISO metric fine (core hole entry is calculated exactly by diameter - pitch) */
    {
        { "M1x0.2",     1.0, 0.20,  0.80 },
        { "M1.1x0.2",   1.1, 0.20,  0.90 },
        { "M1.2x0.2",   1.2, 0.20,  1.00 },
        { "M1.4x0.2",   1.4, 0.20,  1.20 },
        { "M1.6x0.2",   1.6, 0.20,  1.40 },
        { "M1.8x0.2",   1.8, 0.20,  1.60 },
        { "M2x0.25",    2.0, 0.25,  1.75 },
        { "M2.2x0.25",  2.2, 0.25,  1.95 },
        { "M2.5x0.35",  2.5, 0.35,  2.15 },
        { "M3x0.35",    3.0, 0.35,  2.65 },
        { "M3.5x0.35",  3.5, 0.35,  3.15 },
        { "M4x0.5",     4.0, 0.50,  3.50 },
        { "M4.5x0.5",   4.5, 0.50,  4.00 },
        { "M5x0.5",     5.0, 0.50,  4.50 },
        { "M5.5x0.5",   5.5, 0.50,  5.00 },
        { "M6x0.75",    6.0, 0.75,  5.25 },
        { "M7x0.75",    7.0, 0.75,  6.25 },
        { "M8x0.75",    8.0, 0.75,  7.25 },
        { "M8x1.0",     8.0, 1.00,  7.00 },
        { "M9x0.75",    9.0, 0.75,  8.25 },
        { "M9x1.0",     9.0, 1.00,  8.00 },
        { "M10x0.75",  10.0, 0.75,  9.25 },
        { "M10x1.0",   10.0, 1.00,  9.00 },
        { "M10x1.25",  10.0, 1.25,  8.75 },
        { "M11x0.75",  11.0, 0.75, 10.25 },
        { "M11x1.0",   11.0, 1.00, 10.00 },
        { "M12x1.0",   12.0, 1.00, 11.00 },
        { "M12x1.25",  12.0, 1.25, 10.75 },
        { "M12x1.5",   12.0, 1.50, 10.50 },
        { "M14x1.0",   14.0, 1.00, 13.00 },
        { "M14x1.25",  14.0, 1.25, 12.75 },
        { "M14x1.5",   14.0, 1.50, 12.50 },
        { "M15x1.0",   15.0, 1.00, 14.00 },
        { "M15x1.5",   15.0, 1.50, 13.50 },
        { "M16x1.0",   16.0, 1.00, 15.00 },
        { "M16x1.5",   16.0, 1.50, 14.50 },
        { "M17x1.0",   17.0, 1.00, 16.00 },
        { "M17x1.5",   17.0, 1.50, 15.50 },
        { "M18x1.0",   18.0, 1.00, 17.00 },
        { "M18x1.5",   18.0, 1.50, 16.50 },
        { "M18x2.0",   18.0, 2.00, 16.00 },
        { "M20x1.0",   20.0, 1.00, 19.00 },
        { "M20x1.5",   20.0, 1.50, 18.50 },
        { "M20x2.0",   20.0, 2.00, 18.00 },
        { "M22x1.0",   22.0, 1.00, 21.00 },
        { "M22x1.5",   22.0, 1.50, 20.50 },
        { "M22x2.0",   22.0, 2.00, 20.00 },
        { "M24x1.0",   24.0, 1.00, 23.00 },
        { "M24x1.5",   24.0, 1.50, 22.50 },
        { "M24x2.0",   24.0, 2.00, 22.00 },
        { "M25x1.0",   25.0, 1.00, 24.00 },
        { "M25x1.5",   25.0, 1.50, 23.50 },
        { "M25x2.0",   25.0, 2.00, 23.00 },
        { "M27x1.0",   27.0, 1.00, 26.00 },
        { "M27x1.5",   27.0, 1.50, 25.50 },
        { "M27x2.0",   27.0, 2.00, 25.00 },
        { "M28x1.0",   28.0, 1.00, 27.00 },
        { "M28x1.5",   28.0, 1.50, 26.50 },
        { "M28x2.0",   28.0, 2.00, 26.00 },
        { "M30x1.0",   30.0, 1.00, 29.00 },
        { "M30x1.5",   30.0, 1.50, 28.50 },
        { "M30x2.0",   30.0, 2.00, 28.00 },
        { "M30x3.0",   30.0, 3.00, 27.00 },
        { "M32x1.5",   32.0, 1.50, 30.50 },
        { "M32x2.0",   32.0, 2.00, 30.00 },
        { "M33x1.5",   33.0, 1.50, 31.50 },
        { "M33x2.0",   33.0, 2.00, 31.00 },
        { "M33x3.0",   33.0, 3.00, 30.00 },
        { "M35x1.5",   35.0, 1.50, 33.50 },
        { "M35x2.0",   35.0, 2.00, 33.00 },
        { "M36x1.5",   36.0, 1.50, 34.50 },
        { "M36x2.0",   36.0, 2.00, 34.00 },
        { "M36x3.0",   36.0, 3.00, 33.00 },
        { "M39x1.5",   39.0, 1.50, 37.50 },
        { "M39x2.0",   39.0, 2.00, 37.00 },
        { "M39x3.0",   39.0, 3.00, 36.00 },
        { "M40x1.5",   40.0, 1.50, 38.50 },
        { "M40x2.0",   40.0, 2.00, 38.00 },
        { "M40x3.0",   40.0, 3.00, 37.00 },
        { "M42x1.5",   42.0, 1.50, 40.50 },
        { "M42x2.0",   42.0, 2.00, 40.00 },
        { "M42x3.0",   42.0, 3.00, 39.00 },
        { "M42x4.0",   42.0, 4.00, 38.00 },
        { "M45x1.5",   45.0, 1.50, 43.50 },
        { "M45x2.0",   45.0, 2.00, 43.00 },
        { "M45x3.0",   45.0, 3.00, 42.00 },
        { "M45x4.0",   45.0, 4.00, 41.00 },
        { "M48x1.5",   48.0, 1.50, 46.50 },
        { "M48x2.0",   48.0, 2.00, 46.00 },
        { "M48x3.0",   48.0, 3.00, 45.00 },
        { "M48x4.0",   48.0, 4.00, 44.00 },
        { "M50x1.5",   50.0, 1.50, 48.50 },
        { "M50x2.0",   50.0, 2.00, 48.00 },
        { "M50x3.0",   50.0, 3.00, 47.00 },
        { "M52x1.5",   52.0, 1.50, 50.50 },
        { "M52x2.0",   52.0, 2.00, 50.00 },
        { "M52x3.0",   52.0, 3.00, 49.00 },
        { "M52x4.0",   52.0, 4.00, 48.00 },
        { "M55x1.5",   55.0, 1.50, 53.50 },
        { "M55x2.0",   55.0, 2.00, 53.00 },
        { "M55x3.0",   55.0, 3.00, 52.00 },
        { "M55x4.0",   55.0, 4.00, 51.00 },
        { "M56x1.5",   56.0, 1.50, 54.50 },
        { "M56x2.0",   56.0, 2.00, 54.00 },
        { "M56x3.0",   56.0, 3.00, 53.00 },
        { "M56x4.0",   56.0, 4.00, 52.00 },
        { "M58x1.5",   58.0, 1.50, 56.50 },
        { "M58x2.0",   58.0, 2.00, 56.00 },
        { "M58x3.0",   58.0, 3.00, 55.00 },
        { "M58x4.0",   58.0, 4.00, 54.00 },
        { "M60x1.5",   60.0, 1.50, 58.50 },
        { "M60x2.0",   60.0, 2.00, 58.00 },
        { "M60x3.0",   60.0, 3.00, 57.00 },
        { "M60x4.0",   60.0, 4.00, 56.00 },
        { "M62x1.5",   62.0, 1.50, 60.50 },
        { "M62x2.0",   62.0, 2.00, 60.00 },
        { "M62x3.0",   62.0, 3.00, 59.00 },
        { "M62x4.0",   62.0, 4.00, 58.00 },
        { "M64x1.5",   64.0, 1.50, 62.50 },
        { "M64x2.0",   64.0, 2.00, 62.00 },
        { "M64x3.0",   64.0, 3.00, 61.00 },
        { "M64x4.0",   64.0, 4.00, 60.00 },
        { "M65x1.5",   65.0, 1.50, 63.50 },
        { "M65x2.0",   65.0, 2.00, 63.00 },
        { "M65x3.0",   65.0, 3.00, 62.00 },
        { "M65x4.0",   65.0, 4.00, 61.00 },
        { "M68x1.5",   68.0, 1.50, 66.50 },
        { "M68x2.0",   68.0, 2.00, 66.00 },
        { "M68x3.0",   68.0, 3.00, 65.00 },
        { "M68x4.0",   68.0, 4.00, 64.00 },
        { "M70x1.5",   70.0, 1.50, 68.50 },
        { "M70x2.0",   70.0, 2.00, 68.00 },
        { "M70x3.0",   70.0, 3.00, 67.00 },
        { "M70x4.0",   70.0, 4.00, 66.00 },
        { "M70x6.0",   70.0, 6.00, 64.00 },
        { "M72x1.5",   72.0, 1.50, 70.50 },
        { "M72x2.0",   72.0, 2.00, 70.00 },
        { "M72x3.0",   72.0, 3.00, 69.00 },
        { "M72x4.0",   72.0, 4.00, 68.00 },
        { "M72x6.0",   72.0, 6.00, 66.00 },
        { "M75x1.5",   75.0, 1.50, 73.50 },
        { "M75x2.0",   75.0, 2.00, 73.00 },
        { "M75x3.0",   75.0, 3.00, 72.00 },
        { "M75x4.0",   75.0, 4.00, 71.00 },
        { "M75x6.0",   75.0, 6.00, 69.00 },
        { "M76x1.5",   76.0, 1.50, 74.50 },
        { "M76x2.0",   76.0, 2.00, 74.00 },
        { "M76x3.0",   76.0, 3.00, 73.00 },
        { "M76x4.0",   76.0, 4.00, 72.00 },
        { "M76x6.0",   76.0, 6.00, 70.00 },
        { "M80x1.5",   80.0, 1.50, 78.50 },
        { "M80x2.0",   80.0, 2.00, 78.00 },
        { "M80x3.0",   80.0, 3.00, 77.00 },
        { "M80x4.0",   80.0, 4.00, 76.00 },
        { "M80x6.0",   80.0, 6.00, 74.00 },
        { "M85x2.0",   85.0, 2.00, 83.00 },
        { "M85x3.0",   85.0, 3.00, 82.00 },
        { "M85x4.0",   85.0, 4.00, 81.00 },
        { "M85x6.0",   85.0, 6.00, 79.00 },
        { "M90x2.0",   90.0, 2.00, 88.00 },
        { "M90x3.0",   90.0, 3.00, 87.00 },
        { "M90x4.0",   90.0, 4.00, 86.00 },
        { "M90x6.0",   90.0, 6.00, 84.00 },
        { "M95x2.0",   95.0, 2.00, 93.00 },
        { "M95x3.0",   95.0, 3.00, 92.00 },
        { "M95x4.0",   95.0, 4.00, 91.00 },
        { "M95x6.0",   95.0, 6.00, 89.00 },
        { "M100x2.0", 100.0, 2.00, 98.00 },
        { "M100x3.0", 100.0, 3.00, 97.00 },
        { "M100x4.0", 100.0, 4.00, 96.00 },
        { "M100x6.0", 100.0, 6.00, 94.00 }
     },
    /* UNC */
    {
        { "#1",         1.854,  0.397,     1.50 },
        { "#2",         2.184,  0.454,     1.85 },
        { "#3",         2.515,  0.529,     2.10 },
        { "#4",         2.845,  0.635,     2.35 },
        { "#5",         3.175,  0.635,     2.65 },
        { "#6",         3.505,  0.794,     2.85 },
        { "#8",         4.166,  0.794,     3.50 },
        { "#10",        4.826,  1.058,     3.90 },
        { "#12",        5.486,  1.058,     4.50 },
        { "1/4",        6.350,  1.270,     5.10 },
        { "5/16",       7.938,  1.411,     6.60 },
        { "3/8",        9.525,  1.588,     8.00 },
        { "7/16",       11.113, 1.814,     9.40 },
        { "1/2",        12.700, 1.954,    10.80 },
        { "9/16",       14.288, 2.117,    12.20 },
        { "5/8",        15.875, 2.309,    13.50 },
        { "3/4",        19.050, 2.540,    16.50 },
        { "7/8",        22.225, 2.822,    19.50 },
        { "1",          25.400, 3.175,    22.25 },
        { "1 1/8",      28.575, 3.628,    25.00 },
        { "1 1/4",      31.750, 3.628,    28.00 },
        { "1 3/8",      34.925, 4.233,    30.75 },
        { "1 1/2",      38.100, 4.233,    34.00 },
        { "1 3/4",      44.450, 5.080,    39.50 },
        { "2",          50.800, 5.644,    45.00 },
        { "2 1/4",      57.150, 5.644,    51.50 },
        { "2 1/2",      63.500, 6.350,    57.00 },
        { "2 3/4",      69.850, 6.350,    63.50 },
        { "3",          76.200, 6.350,    70.00 },
        { "3 1/4",      82.550, 6.350,    76.50 },
        { "3 1/2",      88.900, 6.350,    83.00 },
        { "3 3/4",      95.250, 6.350,    89.00 },
        { "4",          101.600, 6.350,   95.50 },
    },
    /* UNF */
    {
        { "#0",         1.524,  0.317,    1.20 },
        { "#1",         1.854,  0.353,    1.55 },
        { "#2",         2.184,  0.397,    1.85 },
        { "#3",         2.515,  0.454,    2.10 },
        { "#4",         2.845,  0.529,    2.40 },
        { "#5",         3.175,  0.577,    2.70 },
        { "#6",         3.505,  0.635,    2.95 },
        { "#8",         4.166,  0.706,    3.50 },
        { "#10",        4.826,  0.794,    4.10 },
        { "#12",        5.486,  0.907,    4.70 },
        { "1/4",        6.350,  0.907,    5.50 },
        { "5/16",       7.938,  1.058,    6.90 },
        { "3/8",        9.525,  1.058,    8.50 },
        { "7/16",       11.113, 1.270,    9.90 },
        { "1/2",        12.700, 1.270,    11.50 },
        { "9/16",       14.288, 1.411,    12.90 },
        { "5/8",        15.875, 1.411,    14.50 },
        { "3/4",        19.050, 1.588,    17.50 },
        { "7/8",        22.225, 1.814,    20.40 },
        { "1",          25.400, 2.117,    23.25 },
        { "1 1/8",      28.575, 2.117,    26.50 },
        { "1 1/4",      31.750, 2.117,    29.50 },
        { "1 3/8",      34.925, 2.117,    32.75 },
        { "1 1/2",      38.100, 2.117,    36.00 },
    }    ,
    /* UNEF */
    {
        { "#12",        5.486,  0.794,    4.80 },
        { "1/4",        6.350,  0.794,    5.70 },
        { "5/16",       7.938,  0.794,    7.25 },
        { "3/8",        9.525,  0.794,    8.85 },
        { "7/16",       11.113, 0.907,   10.35 },
        { "1/2",        12.700, 0.907,   11.80 },
        { "9/16",       14.288, 1.058,   13.40 },
        { "5/8",        15.875, 1.058,   15.00 },
        { "11/16",      17.462, 1.058,   16.60 },
        { "3/4",        19.050, 1.270,   18.00 },
        { "13/16",      20.638, 1.270,   19.60 },
        { "7/8",        22.225, 1.270,   21.15 },
        { "15/16",      23.812, 1.270,   22.70 },
        { "1",          25.400, 1.270,   24.30 },
        { "1 1/16",     26.988, 1.411,   25.80 },
        { "1 1/8",      28.575, 1.411,   27.35 },
        { "1 1/4",      31.750, 1.411,   30.55 },
        { "1 5/16",     33.338, 1.411,   32.10 },
        { "1 3/8",      34.925, 1.411,   33.70 },
        { "1 7/16",     36.512, 1.411,   35.30 },
        { "1 1/2",      38.100, 1.411,   36.90 },
        { "1 9/16",     39.688, 1.411,   38.55 },
        { "1 5/8",      41.275, 1.411,   40.10 },
        { "1 11/16",    42.862, 1.411,   41.60 },
    },
    /* NPT National pipe threads */
    // Asme B1.20.1
    {
        { "1/16",   7.938,      0.941,  0.0   },
        { "1/8",    10.287,     0.941,  0.0   },
        { "1/4",    13.716,     1.411,  0.0   },
        { "3/8",    17.145,     1.411,  0.0   },
        { "1/2",    21.336,     1.814,  0.0   },
        { "3/4",    26.670,     1.814,  0.0   },
        { "1",      33.401,     2.209,  0.0   },
        { "1 1/4",  42.164,     2.209,  0.0   },
        { "1 1/2",  48.260,     2.209,  0.0   },
        { "2",      60.325,     2.209,  0.0   },
        { "2 1/2",  73.025,     3.175,  0.0   },
        { "3",      88.900,     3.175,  0.0   },
        { "3 1/2",  101.600,    3.175,  0.0   },
        { "4",      114.300,    3.175,  0.0   },
        { "5",      141.300,    3.175,  0.0   },
        { "6",      168.275,    3.175,  0.0   },
        { "8",      219.075,    3.175,  0.0   },
        { "10",     273.050,    3.175,  0.0   },
        { "12",     323.850,    3.175,  0.0   },
    },
    /* BSP */
    // Parallel - ISO 228-1
    // Tapered  - ISO 7-1
    {
        { "1/16",   7.723,      0.907,   6.6     },
        { "1/8",    9.728,      0.907,   8.8     },
        { "1/4",    13.157,     1.337,   11.8    },
        { "3/8",    16.662,     1.337,   15.25   },
        { "1/2",    20.955,     1.814,   19.00   },
        { "5/8",    22.911,     1.814,   21.00   },
        { "3/4",    26.441,     1.814,   24.50   },
        { "7/8",    30.201,     1.814,   28.25   },
        { "1",      33.249,     2.309,   30.75   },
        { "1 1/8",  37.897,     2.309,   0.0     },
        { "1 1/4",  41.910,     2.309,   39.50   },
        { "1 1/2",  47.803,     2.309,   45.50   },
        { "1 3/4",  53.743,     2.309,   51.00   },
        { "2",      59.614,     2.309,   57.00   },
        { "2 1/4",  65.710,     2.309,   0.0     },
        { "2 1/2",  75.184,     2.309,   0.0     },
        { "2 3/4",  81.534,     2.309,   0.0     },
        { "3",      87.884,     2.309,   0.0     },
        { "3 1/2",  100.330,    2.309,   0.0     },
        { "4",      113.030,    2.309,   0.0     },
        { "4 1/2",  125.730,    2.309,   0.0     },
        { "5",      138.430,    2.309,   0.0     },
        { "5 1/2",  151.130,    2.309,   0.0     },
        { "6",      163.830,    2.309,   0.0     },
    },
    /* BSW */
    // BS 84 Basic sizes
    {
        { "1/8",    3.175,    0.635,   2.55  },
        { "3/16",   4.762,    1.058,   3.70  },
        { "1/4",    6.350,    1.270,   5.10  },
        { "5/16",   7.938,    1.411,   6.50  },
        { "3/8",    9.525,    1.588,   7.90  },
        { "7/16",   11.113,   1.814,   9.30  },
        { "1/2",    12.700,   2.117,   10.50 },
        { "9/16",   14.290,   2.117,   12.10 },
        { "5/8",    15.876,   2.309,   13.50 },
        { "11/16",  17.463,   2.309,   15.00 },
        { "3/4",    19.051,   2.540,   16.25 },
        { "7/8",    22.226,   2.822,   19.25 },
        { "1",      25.400,   3.175,   22.00 },
        { "1 1/8",  28.576,   3.629,   24.75 },
        { "1 1/4",  31.751,   3.629,   28.00 },
        { "1 1/2",  38.100,   4.233,   33.50 },
        { "1 3/4",  44.452,   5.080,   39.00 },
        { "2",      50.802,   5.644,   44.50 },
        { "2 1/4",  57.152,   6.350,   0.0 },
        { "2 1/2",  63.502,   6.350,   0.0 },
        { "2 3/4",  69.853,   7.257,   0.0 },
        { "3",      76.203,   7.257,   0.0 },
        { "3 1/4",  82.553,   7.815,   0.0 },
        { "3 1/2",  88.903,   7.815,   0.0 },
        { "3 3/4",  95.254,   8.467,   0.0 },
        { "4",      101.604,  8.467,   0.0 },
        { "4 1/2",  114.304,  8.835,   0.0 },
        { "5",      127.005,  9.236,   0.0 },
        { "5 1/2",  139.705,  9.676,   0.0 },
        { "6",      152.406,  10.16,   0.0 },
    },
    /* BSF */
    // BS 84 Basic sizes
    // BS 1157 for drill sizes
    {
        { "3/16",   4.763,    0.794,   4.00  },
        { "7/32",   5.558,    0.907,   4.60  },
        { "1/4",    6.350,    0.977,   5.30  },
        { "9/32",   7.142,    0.977,   6.10  },
        { "5/16",   7.938,    1.154,   6.80  },
        { "3/8",    9.525,    1.270,   8.30  },
        { "7/16",   11.113,   1.411,   9.70  },
        { "1/2",    12.700,   1.588,   11.10 },
        { "9/16",   14.288,   1.588,   12.70 },
        { "5/8",    15.875,   1.814,   14.00 },
        { "11/16",  17.463,   1.814,   15.50 },
        { "3/4",    19.050,   2.116,   16.75 },
        { "7/8",    22.225,   2.309,   19.75 },
        { "1",      25.400,   2.540,   22.75 },
        { "1 1/8",  28.575,   2.822,   25.50 },
        { "1 1/4",  31.750,   2.822,   28.50 },
        { "1 3/8",  34.925,   3.175,   31.50 },
        { "1 1/2",  38.100,   3.175,   34.50 },
        { "1 5/8",  41.275,   3.175,   0.0   },
        { "1 3/4",  44.450,   3.629,   0.0   },
        { "2",      50.800,   3.629,   0.0   },
        { "2 1/4",  57.150,   4.233,   0.0   },
        { "2 1/2",  63.500,   4.233,   0.0   },
        { "2 3/4",  69.850,   4.233,   0.0   },
        { "3",      76.200,   5.080,   0.0   },
        { "3 1/4",  82.550,   5.080,   0.0   },
        { "3 1/2",  88.900,   5.644,   0.0   },
        { "3 3/4",  95.250,   5.644,   0.0   },
        { "4",      101.600,  5.644,   0.0   },
        { "4 1/4",  107.950,  6.350,   0.0   },
    },
    /* ISO tyre valve threads, ISO 4570:2002, ordered as the standard
       (upstream 551c15b48f) */
    // {name, thread diameter, thread pitch, core hole diameter}
    {
        { "5v1",  5.334, 0.705, 0.0 }, // Schrader internal
        { "5v2",  5.370, 1.058, 0.0 }, // Presta cap
        { "6v1",  6.160, 0.800, 0.0 }, // Presta body
        { "8v1",  7.798, 0.794, 0.0 }, // Schrader external
        { "9v1",  9.525, 0.794, 0.0 },
        { "10v2", 10.414, 0.907, 0.0 },
        { "12v1", 12.319, 0.977, 0.0 },
        { "13v1", 12.700, 1.270, 0.0 },
        { "8v2",  7.938, 1.058, 0.0 },
        { "10v1", 9.800, 1.000, 0.0 },
        { "11v1", 11.113, 1.270, 0.0 },
        { "13v2", 12.700, 0.794, 0.0 },
        { "15v1", 15.137, 1.000, 0.0 },
        { "16v1", 15.875, 0.941, 0.0 },
        { "17v1", 17.137, 1.000, 0.0 },
        { "17v2", 17.463, 1.058, 0.0 },
        { "17v3", 17.463, 1.588, 0.0 },
        { "19v1", 19.050, 1.588, 0.0 },
        { "20v1", 20.642, 1.000, 0.0 },
    }
};

const double Hole::metricHoleDiameters[51][4] =
{
    /* ISO metric clearance hole diameters according to ISO 273 */
    // {screw diameter, fine, medium, coarse}
        { 1.0,      1.1,    1.2,    1.3},
        { 1.2,      1.3,    1.4,    1.5},
        { 1.4,      1.5,    1.6,    1.8},
        { 1.6,      1.7,    1.8,    2.0},
        { 1.8,      2.0,    2.1,    2.2},
        { 2.0,      2.2,    2.4,    2.6},
        { 2.5,      2.7,    2.9,    3.1},
        { 3.0,      3.2,    3.4,    3.6},
        { 3.5,      3.7,    3.9,    4.2},
        { 4.0,      4.3,    4.5,    4.8},
        { 4.5,      4.8,    5.0,    5.3},
        { 5.0,      5.3,    5.5,    5.8},
        { 6.0,      6.4,    6.6,    7.0},
        { 7.0,      7.4,    7.6,    8.0},
        { 8.0,      8.4,    9.0,    10.0},
        // 9.0 undefined
        { 10.0,     10.5,   11.0,   12.0},
        // 11.0 undefined
        { 12.0,     13.0,   13.5,   14.5},
        { 14.0,     15.0,   15.5,   16.5},
        { 16.0,     17.0,  	17.5,   18.5},
        { 18.0,     19.0,  	20.0,   21.0},
        { 20.0,     21.0,  	22.0,   24.0},
        { 22.0,     23.0,  	24.0,   26.0},
        { 24.0,     25.0,  	26.0,   28.0},
        { 27.0,     28.0,  	30.0,   32.0},
        { 30.0,     31.0,  	33.0,   35.0},
        { 33.0,     34.0,  	36.0,   38.0},
        { 36.0,     37.0,  	39.0,   42.0},
        { 39.0,     40.0,  	42.0,   45.0},
        { 42.0,     43.0,  	45.0,   48.0},
        { 45.0,     46.0,  	48.0,   52.0},
        { 48.0,     50.0,  	52.0,   56.0},
        { 52.0,     54.0,  	56.0,   62.0},
        { 56.0,     58.0,  	62.0,   66.0},
        { 60.0,     62.0,  	66.0,   70.0},
        { 64.0,     66.0,  	70.0,   74.0},
        { 68.0,     70.0,  	74.0,   78.0},
        { 72.0,     74.0,  	78.0,   82.0},
        { 76.0,     78.0,  	82.0,   86.0},
        { 80.0,     82.0,  	86.0,   91.0},
        { 85.0,     87.0,  	91.0,   96.0},
        { 90.0,     93.0,  	96.0,   101.0},
        { 95.0,     98.0,  	101.0,   107.0},
        { 100.0,     104.0,  	107.0,   112.0},
        { 105.0,     109.0,  	112.0,   117.0},
        { 110.0,     114.0,  	117.0,   122.0},
        { 115.0,     119.0,  	122.0,   127.0},
        { 120.0,     124.0,  	127.0,   132.0},
        { 125.0,     129.0,  	132.0,   137.0},
        { 130.0,     134.0,  	137.0,   144.0},
        { 140.0,     144.0,  	147.0,   155.0},
        { 150.0,     155.0,  	158.0,   165.0}
};

const Hole::UTSClearanceDefinition Hole::UTSHoleDiameters[22] =
{
    /* UTS clearance hole diameters according to ASME B18.2.8 */
    // for information: the norm defines a drill bit number (that is in turn standardized in another ASME norm).
    // as result the norm defines a minimal clearance which is the diameter of that drill bit.
    // we use here this minimal clearance as the theoretical exact hole diameter as this is also done in the ISO norm.
    // {screw class, close, normal, loose}
        { "#0",     1.7,  1.9,  2.4 },
        { "#1",     2.1,  2.3,  2.6 },
        { "#2",     2.4,  2.6,  2.9 },
        { "#3",     2.7,  2.9,  3.3 },
        { "#4",     3.0,  3.3,  3.7 },
        { "#5",     3.6,  4.0,  4.4 },
        { "#6",     3.9,  4.3,  4.7 },
        { "#8",     4.6,  5.0,  5.4 },
        { "#10",    5.2,  5.6,  6.0 },
        // "#12" not defined
        { "1/4",    6.8,  7.1,  7.5 },
        { "5/16",   8.3,  8.7,  9.1 },
        { "3/8",    9.9, 10.3, 10.7 },
        { "7/16",  11.5, 11.9, 12.3 },
        { "1/2",   13.5, 14.3, 15.5 },
        // "9/16" not defined
        { "5/8",   16.7, 17.5, 18.6 },
        { "3/4",   19.8, 20.6, 23.0 },
        { "7/8",   23.0, 23.8, 26.2 },
        { "1",     26.2, 27.8, 29.4 },
        { "1 1/8", 29.4, 31.0, 33.3 },
        { "1 1/4", 32.5, 34.1, 36.5 },
        { "1 3/8", 36.5, 38.1, 40.9 },
        { "1 1/2", 39.7, 41.3, 44.0 }
};

std::vector<std::string> getThreadDesignations(const int threadType) {
    std::vector<std::string> designations;
    for (const auto& thread : Hole::threadDescription[threadType]) {
        if (thread.designation != nullptr) {
            designations.emplace_back(thread.designation);
        }
    }
    return designations;
}

/* ISO coarse metric enums */
std::vector<std::string> Hole::HoleCutType_ISOmetric_Enums = {
    "None",
    "Counterbore",
    "Countersink",
    "Counterdrill"};
const char* Hole::ThreadClass_ISOmetric_Enums[]  = { "4G", "4H", "5G", "5H", "6G", "6H", "7G", "7H","8G", "8H", nullptr };

std::vector<std::string> Hole::HoleCutType_ISOmetricfine_Enums = {
    "None",
    "Counterbore",
    "Countersink",
    "Counterdrill"};
const char* Hole::ThreadClass_ISOmetricfine_Enums[]  = { "4G", "4H", "5G", "5H", "6G", "6H", "7G", "7H","8G", "8H", nullptr };

// ISO 965-1:2013 ISO general purpose metric screw threads - Tolerances - Part 1
// Table 1 - Fundamentral deviations for internal threads ...
// reproduced in: https://www.accu.co.uk/en/p/134-iso-metric-thread-tolerances [retrieved: 2021-01-11]
const double Hole::ThreadClass_ISOmetric_data[ThreadClass_ISOmetric_data_size][2] = {
//  Pitch    G
    {0.2,   0.017},
    {0.25,  0.018},
    {0.3,   0.018},
    {0.35,  0.019},
    {0.4,   0.019},
    {0.45,  0.020},
    {0.5,   0.020},
    {0.6,   0.021},
    {0.7,   0.022},
    {0.75,  0.022},
    {0.8,   0.024},
    {1.0,   0.026},
    {1.25,  0.028},
    {1.5,   0.032},
    {1.75,  0.034},
    {2.0,   0.038},
    {2.5,   0.042},
    {3.0,   0.048},
    {3.5,   0.053},
    {4.0,   0.060},
    {4.5,   0.063},
    {5.0,   0.071},
    {5.5,   0.075},
    {6.0,   0.080},
    {8.0,   0.100}
};

/* According to DIN 76-1 (Thread run-outs and thread undercuts - Part 1: For ISO metric threads in accordance with DIN 13-1) */
const double Hole::ThreadRunout[ThreadRunout_size][2] = {
//  Pitch    e1
    {0.2,   1.3},
    {0.25,  1.5},
    {0.3,   1.8},
    {0.35,  2.1},
    {0.4,   2.3},
    {0.45,  2.6},
    {0.5,   2.8},
    {0.6,   3.4},
    {0.7,   3.8},
    {0.75,  4.0},
    {0.8,   4.2},
    {1.0,   5.1},
    {1.25,  6.2},
    {1.5,   7.3},
    {1.75,  8.3},
    {2.0,   9.3},
    {2.5,   11.2},
    {3.0,   13.1},
    {3.5,   15.2},
    {4.0,   16.8},
    {4.5,   18.4},
    {5.0,   20.8},
    {5.5,   22.4},
    {6.0,   24.0}
};

/* Details from https://en.wikipedia.org/wiki/Unified_Thread_Standard */

/* UTS coarse */
const char* Hole::HoleCutType_UNC_Enums[]  = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNC_Enums[]  = { "1B", "2B", "3B", nullptr };

/* UTS fine */
const char* Hole::HoleCutType_UNF_Enums[]  = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNF_Enums[]  = { "1B", "2B", "3B", nullptr };

/* UTS extrafine */
const char* Hole::HoleCutType_UNEF_Enums[] = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNEF_Enums[] = { "1B", "2B", "3B", nullptr };

/* NPT */
const char* Hole::HoleCutType_NPT_Enums[] = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};

/* BSP */
const char* Hole::HoleCutType_BSP_Enums[] = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};

/* BSW */
const char* Hole::HoleCutType_BSW_Enums[] = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_BSW_Enums[] = { "Medium", "Normal", nullptr };

/* BSF */
const char* Hole::HoleCutType_BSF_Enums[] = { "None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_BSF_Enums[] = { "Medium", "Normal", nullptr };

const char* Hole::ThreadDirectionEnums[]  = { "Right", "Left", nullptr};

PROPERTY_SOURCE(PartDesign::Hole, PartDesign::ProfileBased)

const App::PropertyAngle::Constraints Hole::floatAngle = { Base::toDegrees<double>(Precision::Angular()), 180.0 - Base::toDegrees<double>(Precision::Angular()), 1.0 };
// OCC can only create holes with a min diameter of 10 times the Precision::Confusion()
const App::PropertyQuantityConstraint::Constraints diameterRange = { 10 * Precision::Confusion(), FLT_MAX, 1.0 };
// A custom thread clearance may be negative as well as positive, to adjust
// for manufacturing (upstream c8efc26982; a PropertyLength stops at 0)
const App::PropertyQuantityConstraint::Constraints clearanceRange = { -FLT_MAX, FLT_MAX, 0.1 };

Hole::Hole()
{
    initAddSubType(FeatureAddSub::Subtractive);

    readCutDefinitions();

    ADD_PROPERTY_TYPE(Threaded, (false), "Hole", App::Prop_None, "Threaded");

    ADD_PROPERTY_TYPE(ModelThread, (false), "Hole", App::Prop_None, "Model actual thread");

    // Upstream's name and default (180c39709a), so a file travels both ways
    ADD_PROPERTY_TYPE(CosmeticThread, (true), "Hole", App::Prop_None,
                      "Draw the thread on the bore without modelling it");

    ADD_PROPERTY_TYPE(ThreadType, (0L), "Hole", App::Prop_None, "Thread type");
    ThreadType.setEnums(ThreadTypeEnums);

    ADD_PROPERTY_TYPE(ThreadSize, (0L), "Hole", App::Prop_None, "Thread size");
    ThreadSize.setEnums(getThreadDesignations(ThreadType.getValue()));

    ADD_PROPERTY_TYPE(ThreadClass, (0L), "Hole", App::Prop_None, "Thread class");
    ThreadClass.setEnums(ThreadClass_None_Enums);

    ADD_PROPERTY_TYPE(ThreadFit, (0L), "Hole", App::Prop_None, "Clearance hole fit");
    ThreadFit.setEnums(ClearanceMetricEnums);

    // Hide the default ProfileBased::Fit as the hole feature has its own processing
    this->Fit.setStatus(App::Property::Hidden, true);
    this->InnerFit.setStatus(App::Property::Hidden, true);
    this->FitJoin.setStatus(App::Property::Hidden, true);
    this->InnerFitJoin.setStatus(App::Property::Hidden, true);

    ADD_PROPERTY_TYPE(Diameter, (6.0), "Hole", App::Prop_None, "Diameter");
    Diameter.setConstraints(&diameterRange);

    ADD_PROPERTY_TYPE(ThreadDiameter, (0.0), "Hole", App::Prop_None, "Thread major diameter");
    ThreadDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(ThreadDirection, (0L), "Hole", App::Prop_None, "Thread direction");
    ThreadDirection.setEnums(ThreadDirectionEnums);
    ThreadDirection.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutType, (0L), "Hole", App::Prop_None, "Head cut type");
    HoleCutType.setEnums(HoleCutType_None_Enums);

    ADD_PROPERTY_TYPE(HoleCutCustomValues, (false), "Hole", App::Prop_None, "Custom cut values");
    HoleCutCustomValues.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDiameter, (0.0), "Hole", App::Prop_None, "Head cut diameter");
    HoleCutDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDepth, (0.0), "Hole", App::Prop_None, "Head cut depth");
    HoleCutDepth.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutCountersinkAngle, (90.0), "Hole", App::Prop_None, "Head cut countersink angle");
    HoleCutCountersinkAngle.setConstraints(&floatAngle);
    HoleCutCountersinkAngle.setReadOnly(true);

    ADD_PROPERTY_TYPE(DepthType, (0L), "Hole", App::Prop_None, "Type");
    DepthType.setEnums(DepthTypeEnums);

    ADD_PROPERTY_TYPE(Depth, (25.0), "Hole", App::Prop_None, "Length");

    ADD_PROPERTY_TYPE(DrillPoint, (1L), "Hole", App::Prop_None, "Drill point type");
    DrillPoint.setEnums(DrillPointEnums);

    ADD_PROPERTY_TYPE(DrillPointAngle, (118.0), "Hole", App::Prop_None, "Drill point angle");
    DrillPointAngle.setConstraints(&floatAngle);
    ADD_PROPERTY_TYPE(DrillForDepth, ((long)0), "Hole", App::Prop_None,
        "The size of the drill point will be taken into\n account for the depth of blind holes");

    ADD_PROPERTY_TYPE(Tapered, (false), "Hole", App::Prop_None, "Tapered");

    ADD_PROPERTY_TYPE(TaperedAngle, (90.0), "Hole", App::Prop_None, "Tapered angle");
    TaperedAngle.setConstraints(&floatAngle);

    ADD_PROPERTY_TYPE(ThreadDepthType, (0L), "Hole", App::Prop_None, "Thread depth type");
    ThreadDepthType.setEnums(ThreadDepthTypeEnums);

    ADD_PROPERTY_TYPE(ThreadDepth, (23.5), "Hole", App::Prop_None, "Thread Length"); // default is assuming an M1

    ADD_PROPERTY_TYPE(UseCustomThreadClearance, (false), "Hole", App::Prop_None, "Use custom thread clearance");

    ADD_PROPERTY_TYPE(CustomThreadClearance, (0.0), "Hole", App::Prop_None, "Custom thread clearance (overrides ThreadClass)");
    CustomThreadClearance.setConstraints(&clearanceRange);

    // Circles and arcs, as a file without the property was made with; a new
    // hole takes the preference in setupObject() (upstream 774ec2cc93)
    ADD_PROPERTY_TYPE(BaseProfileType, (BaseProfileTypeOptions::OnCirclesArcs), "Hole", App::Prop_None,
                      "What of the profile the holes are centred on: 1 points, 2 circles, 4 arcs, summed");
    // Upstream f394f1b669's properties, under its names
    ADD_PROPERTY_TYPE(StartType, (0L), "Start", App::Prop_None, "How to define the start plane");
    StartType.setEnums(StartTypesEnums);
    ADD_PROPERTY_TYPE(StartOffset, (0.0), "Start", App::Prop_None,
                      "How far into the material from the start plane the holes start");
    ADD_PROPERTY_TYPE(StartReference, (nullptr), "Start", App::Prop_None,
                      "Face, plane or sketch the holes start at, with StartType Reference");
    StartOffset.setReadOnly(true);
    StartReference.setReadOnly(true);
}

double Hole::startOffset(const TopoShape& profileshape, const gp_Dir& holeDirection,
                         const TopLoc_Location& invObjLoc) const
{
    const std::string type = StartType.getValueAsString();
    if (type == "Offset")
        return StartOffset.getValue();
    if (type == "Reference")
        return getStartReferenceOffset(profileshape, StartReference, holeDirection,
                                       StartOffset.getValue(), invObjLoc);
    return 0.0;
}

double Hole::getStartOffset() const
{
    // In global coordinates, where the profile and the reference both are
    TopoShape profileshape = getProfileShape();
    Base::Vector3d normal = guessNormalDirection(profileshape);
    if (!Reversed.getValue())
        normal *= -1.0;
    return startOffset(profileshape, gp_Dir(normal.x, normal.y, normal.z), TopLoc_Location());
}

void Hole::setupObject()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/PartDesign");
    int bits = baseProfileOption_idxToBitmask(hGrp->GetInt("defaultBaseTypeHole", 1));
    BaseProfileType.setValue(bits > 0 ? bits : int(BaseProfileTypeOptions::OnPointsCirclesArcs));
    ProfileBased::setupObject();
}

int Hole::baseProfileOption_idxToBitmask(int index)
{
    switch (index) {
    case 0:
        return OnCirclesArcs;
    case 1:
        return OnPointsCirclesArcs;
    case 2:
        return OnPoints;
    default:
        return -1;
    }
}

int Hole::baseProfileOption_bitmaskToIdx(int bitmask)
{
    switch (bitmask) {
    case OnCirclesArcs:
        return 0;
    case OnPointsCirclesArcs:
        return 1;
    case OnPoints:
        return 2;
    default:
        return -1;
    }
}

static inline bool _isRestoring(const App::Document *doc)
{
    return !doc || doc->testStatus(App::Document::Restoring) || doc->isPerformingTransaction();
}

void Hole::updateHoleCutParams()
{
    if (_isRestoring(getDocument()))
        return;

    std::string holeCutTypeStr = HoleCutType.getValueAsString();

    // there is no cut, thus return
    if (holeCutTypeStr == "None")
        return;

    if (ThreadType.getValue() < 0) {
        THROWM(Base::IndexError, "Thread type out of range")
        return;
    }

    // get diameter and size
    double diameterVal = Diameter.getValue();

    // handle thread types
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
        if (ThreadSize.getValue() < 0) {
            THROWM(Base::IndexError, "Thread size out of range")
            return;
        }

        std::string threadSizeStr = ThreadSize.getValueAsString();

        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we take in this case the values from the norm ISO 4762 or ISO 10642
        if (holeCutTypeStr == "Counterbore") {
            // read ISO 4762 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 4762");
            const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                // there is no norm defining counterbores for all sizes, thus we need to use the
                // same fallback as for the case HoleCutTypeMap.count(key)
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                    HoleCutDepth.setValue(dimen.depth);
                }
                else {
                    calculateAndSetCounterbore();
                }
            }
            if (HoleCutDepth.getValue() == 0.0)
                HoleCutDepth.setValue(dimen.depth);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink"  || holeCutTypeStr == "Counterdrill") {
            // read ISO 10642 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 10642");
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                HoleCutCountersinkAngle.setValue(counter.angle);
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                }
                else {
                    calculateAndSetCountersink();
                }
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(counter.angle);
            }
            if (HoleCutDepth.getValue() == 0.0 && holeCutTypeStr == "Counterdrill") {
                HoleCutDepth.setValue(1.0);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }

        // handle since FreeCAD 0.18 deprecated types that were
        // removed after FreeCAD 0.20
        if (holeCutTypeStr == "Cheesehead (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.6);
            HoleCutDepth.setValue(diameterVal * 0.6);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink socket screw (deprecated)") {
            HoleCutType.setValue("Countersink");
            holeCutTypeStr = "Countersink";
            HoleCutDiameter.setValue(diameterVal * 2.0);
            HoleCutDepth.setValue(diameterVal * 0.0);
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(90.0);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Cap screw (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.5);
            HoleCutDepth.setValue(diameterVal * 1.25);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }

        // cut definition
        CutDimensionKey key{ threadTypeStr, holeCutTypeStr };
        if (HoleCutTypeMap.count(key)) {
            const CutDimensionSet& counter = find_cutDimensionSet(key);
            if (counter.cut_type == CutDimensionSet::Counterbore) {
                // disable HoleCutCountersinkAngle and reset it to ISO's default
                HoleCutCountersinkAngle.setValue(90.0);
                HoleCutCountersinkAngle.setReadOnly(true);
                const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
                if (dimen.thread == "None") {
                    calculateAndSetCounterbore();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDepth.setValue(dimen.depth);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())
                            HoleCutCustomValues.setValue(false);
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
            else if (counter.cut_type == CutDimensionSet::Countersink) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                if (dimen.thread == "None") {
                    // there might be an angle of zero (if no norm exists for the size)
                    if (HoleCutCountersinkAngle.getValue() == 0.0) {
                        HoleCutCountersinkAngle.setValue(counter.angle);
                    }
                    calculateAndSetCountersink();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        HoleCutCountersinkAngle.setValue(counter.angle);
                        HoleCutCountersinkAngle.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())
                            HoleCutCustomValues.setValue(false);
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
        }

    }
    else {
        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we use rules of thumbs as proposal
        if (holeCutTypeStr == "Counterbore") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.6);
                HoleCutDepth.setValue(diameterVal * 0.9);
            }
            if (HoleCutDepth.getValue() == 0.0)
                HoleCutDepth.setValue(diameterVal * 0.9);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink" || holeCutTypeStr == "Counterdrill") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.7);
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutDepth.getValue() == 0.0 && holeCutTypeStr == "Counterdrill") {
                HoleCutDepth.setValue(1.0);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
    }
}

double Hole::getCountersinkAngle() const
{
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (
        threadTypeStr == "BSW"
        || threadTypeStr == "BSF"
    )
        return 100.0;
    if (
        threadTypeStr == "UNC"
        || threadTypeStr == "UNF"
        || threadTypeStr == "UNEF"
    )
        return 82.0;
    return 90.0;
}

double Hole::getThreadClassClearance() const
{
    double pitch = getThreadPitch();

    // Calculate how much clearance to add based on Thread tolerance class and pitch
    if (ThreadClass.getValueAsString()[1] == 'G') {
        for (auto it : ThreadClass_ISOmetric_data) {
            double p = it[0];
            if (pitch <= p) {
                return it[1];
            }
        }
    }

    return 0.0;
}

// Calculates the distance between the bottom hole and the bottom most thread.
// This is defined in DIN 76-1, there are 3 possibilities:
// mode=1 (default), For normal cases e1 is wanted.
// mode=2, In cases where shorter thread runout is necessary
// mode=3, In cases where longer thread runout is necessary
double Hole::getThreadRunout(int mode) const
{
    double pitch = getThreadPitch();

    double sf = 1.0;  // scale factor
    switch (mode) {
    case 1:
        sf = 1.0;
        break;
    case 2:
        sf = 0.625;
        break;
    case 3:
        sf = 1.6;
        break;
    default:
        THROWM(Base::ValueError, "Unsupported argument")
    }
    for (auto it : ThreadRunout) {
        double p = it[0];
        if (pitch <= p) {
            return sf * it[1];
        }
    }

    // For non-standard pitch we fall back on general engineering rule of thumb of 4*pitch.
    return 4 * pitch;
}

double Hole::getThreadPitch() const
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        THROWM(Base::IndexError, "Thread type out of range")
    }
    if (threadSize < 0) {
        THROWM(Base::IndexError, "Thread size out of range")
    }
    return threadDescription[threadType][threadSize].pitch;
}

void Hole::updateThreadDepthParam()
{
    if (_isRestoring(getDocument()))
        return;

    std::string ThreadMethod(ThreadDepthType.getValueAsString());
    std::string HoleDepth(DepthType.getValueAsString());
    if (HoleDepth == "Dimension") {
        if (ThreadMethod == "Hole Depth") {
            ThreadDepth.setValue(Depth.getValue());
        }
        else if (ThreadMethod == "Dimension") {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > Depth.getValue())
                ThreadDepth.setValue(Depth.getValue());
            else
                ThreadDepth.setValue(ThreadDepth.getValue());
        }
        else if (ThreadMethod == "Tapped (DIN76)") {
            ThreadDepth.setValue(Depth.getValue() - getThreadRunout());
        }
        else {
            THROWM(Base::RuntimeError, "Unsupported thread depth type \n")
        }
    }
    else if (HoleDepth == "ThroughAll") {
        if (ThreadMethod != "Dimension") {
            ThreadDepth.setValue(getThroughAllLength());
        }
        else {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > getThroughAllLength())
                ThreadDepth.setValue(getThroughAllLength());
            else
                ThreadDepth.setValue(ThreadDepth.getValue());
        }
    }
    else {
        THROWM(Base::RuntimeError, "Unsupported depth type \n")
    }
}

std::optional<double> Hole::determineDiameter() const
{
    if (_isRestoring(getDocument()))
        return std::nullopt;

    // Diameter parameter depends on Threaded, ThreadType, ThreadSize, and ThreadFit

    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        THROWM(Base::IndexError, "Thread type out of range")
    }
    if (threadSize < 0) {
        THROWM(Base::IndexError, "Thread size out of range")
    }
    if (threadType == 0)
        return std::nullopt;

    double diameter = threadDescription[threadType][threadSize].diameter;
    double pitch = threadDescription[threadType][threadSize].pitch;
    double clearance = 0.0;

    if (Threaded.getValue()) {

        if (ModelThread.getValue()) {
            if (UseCustomThreadClearance.getValue())
                clearance = CustomThreadClearance.getValue();
            else
                clearance = getThreadClassClearance();
        }

        // use normed diameters if possible
        std::string threadTypeStr = ThreadType.getValueAsString();
        if (threadDescription[threadType][threadSize].CoreHole > 0) {
            diameter = threadDescription[threadType][threadSize].CoreHole + clearance;
        } // if nothing is available, we must calculate
        else if (
            threadTypeStr == "BSP"
            || threadTypeStr == "BSW"
            || threadTypeStr == "BSF"
        ) {
            double thread = 2 * (0.640327 * pitch);
            // truncation is allowed by ISO-228 and BS 84
            diameter = diameter - thread * 0.75 + clearance;
        }
        else if (threadTypeStr == "NPT") {
            double thread = 2 * (0.8 * pitch);
            diameter = diameter - thread * 0.75 + clearance;
        }
        else {
            // this fits exactly the definition for ISO metric fine
            diameter = diameter - pitch + clearance;
        }
    }
    else { // we have a clearance hole
        bool found = false;
        std::string threadTypeStr = ThreadType.getValueAsString();
        // UTS and metric have a different clearance hole set
        if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
            int MatrixRowSizeMetric = sizeof(metricHoleDiameters) / sizeof(metricHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
            case 0: /* standard fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeMetric; i++) {
                    if (metricHoleDiameters[i][0] == diameter) {
                        diameter = metricHoleDiameters[i][2];
                        found = true;
                        break;
                    }
                }
                // if nothing is defined (e.g. for M2.2, M9 and M11), we must calculate
                // we use the factors defined for M5 in the metricHoleDiameters list
                if (!found) {
                    diameter = diameter * 1.10;
                }
                break;
            case 1: /* close fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeMetric; i++) {
                    if (metricHoleDiameters[i][0] == diameter) {
                        diameter = metricHoleDiameters[i][1];
                        found = true;
                        break;
                    }
                }
                // if nothing was found, we must calculate
                if (!found) {
                    diameter = diameter * 1.06;
                }
                break;
            case 2: /* wide fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeMetric; i++) {
                    if (metricHoleDiameters[i][0] == diameter) {
                        diameter = metricHoleDiameters[i][3];
                        found = true;
                        break;
                    }
                }
                // if nothing was found, we must calculate
                if (!found) {
                    diameter = diameter * 1.16;
                }
                break;
            default:
                THROWM(Base::IndexError, "Thread fit out of range")
            }
        }
        else if (threadTypeStr == "UNC" || threadTypeStr == "UNF" || threadTypeStr == "UNEF") {
            std::string ThreadSizeString = ThreadSize.getValueAsString();
            int MatrixRowSizeUTS = sizeof(UTSHoleDiameters) / sizeof(UTSHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
            case 0: /* normal fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeUTS; i++) {
                    if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                        diameter = UTSHoleDiameters[i].normal;
                        found = true;
                        break;
                    }
                }
                // if nothing was found (if "#12" or "9/16"), we must calculate
                // // we use the factors defined for "3/8" in the UTSHoleDiameters list
                if (!found) {
                    diameter = diameter * 1.08;
                }
                break;
            case 1: /* close fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeUTS; i++) {
                    if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                        diameter = UTSHoleDiameters[i].close;
                        found = true;
                        break;
                    }
                }
                // if nothing was found, we must calculate
                if (!found) {
                    diameter = diameter * 1.04;
                }
                break;
            case 2: /* loose fit */
                // read diameter out of matrix
                for (int i = 0; i < MatrixRowSizeUTS; i++) {
                    if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                        diameter = UTSHoleDiameters[i].loose;
                        found = true;
                        break;
                    }
                }
                // if nothing was found, we must calculate
                if (!found) {
                    diameter = diameter * 1.12;
                }
                break;
            default:
                THROWM(Base::IndexError, "Thread fit out of range")
            }
        }
        else {
            switch (ThreadFit.getValue()) {
            case 0: /* normal fit */
                // we must calculate
                if (!found) {
                    diameter = diameter * 1.1;
                }
                break;
            case 1: /* close fit */
                if (!found) {
                    diameter = diameter * 1.05;
                }
                break;
            case 2: /* loose fit */
                if (!found) {
                    diameter = diameter * 1.15;
                }
                break;
            default:
                THROWM(Base::IndexError, "Thread fit out of range")
            }
        }
    }

    return std::optional<double>{diameter};
}

void Hole::updateDiameterParam()
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    // the first size (index 0) is a size too
    if (threadType > 0 && threadSize >= 0 && !changingThreadType) {
        ThreadDiameter.setValue(
            threadDescription[threadType][threadSize].diameter
        );
        threadPitch = threadDescription[threadType][threadSize].pitch;
    }
    if (auto opt = determineDiameter())
        Diameter.setValue(opt.value());
}

double Hole::getThreadProfileAngle()
{
    // Both ISO 7-1 and ASME B1.20.1 define the same angle
    return 90 - 1.79;
}

void Hole::findClosestDesignation()
{
    // Intended for thread type changes: the size of the new type nearest to
    // the old one's diameter and, among sizes of a diameter, its pitch
    // (upstream dc53d3dba2, 601c0f9b09, 0dc6cbd16f -- which took the pitch
    // from the new type's table at the old size's index)
    int threadType = ThreadType.getValue();
    if (threadType < 0 || threadType >= static_cast<int>(std::size(threadDescription))) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread type is invalid"));
    }
    double diameter = ThreadDiameter.getValue();
    if (diameter == 0.0)
        diameter = Diameter.getValue();

    // The table rows are fixed width here, not upstream's vectors: they end
    // at the first entry without a designation
    const auto &sizes = threadDescription[threadType];
    int closestSize = 0;
    double closest = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < std::size(sizes) && sizes[i].designation; i++) {
        double distance = threadPitch > 0.0
            ? std::hypot(sizes[i].diameter - diameter, sizes[i].pitch - threadPitch)
            : std::abs(sizes[i].diameter - diameter);
        if (distance < closest) {
            closest = distance;
            closestSize = static_cast<int>(i);
        }
    }
    ThreadSize.setValue(closestSize);
}

void Hole::onChanged(const App::Property* prop)
{
    if (prop == &StartType) {
        const std::string type = StartType.getValueAsString();
        StartOffset.setReadOnly(type == "Profile plane");
        StartReference.setReadOnly(type != "Reference");
    }
    else if (prop == &ThreadType) {
        std::string type, holeCutTypeStr;
        if (ThreadType.isValid()) {
            type = ThreadType.getValueAsString();
            {
                // The new list resets the size, which must not replace the
                // diameter and pitch the new size is chosen by
                Base::StateLocker guard(changingThreadType);
                ThreadSize.setEnums(getThreadDesignations(ThreadType.getValue()));
            }
            if (type != "None")
                findClosestDesignation();
        }

        if (HoleCutType.isValid())
            holeCutTypeStr = HoleCutType.getValueAsString();

        if (type == "None") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
            Threaded.setReadOnly(true);
            ThreadSize.setReadOnly(true);
            ThreadFit.setReadOnly(true);
            ThreadClass.setReadOnly(true);
            Diameter.setReadOnly(false);
            ModelThread.setReadOnly(true);
            UseCustomThreadClearance.setReadOnly(true);
            CustomThreadClearance.setReadOnly(true);
            ThreadDepth.setReadOnly(true);
            ThreadDepthType.setReadOnly(true);
            Threaded.setValue(false);
            ModelThread.setValue(false);
            UseCustomThreadClearance.setValue(false);
            ThreadFit.setEnums(ClearanceNoneEnums);
        }
        else if (type == "ISOMetricProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetric_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetric_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            // thread class and direction are only sensible if threaded
            // fit only sensible if not threaded
            ThreadFit.setReadOnly(Threaded.getValue());
            ThreadClass.setReadOnly(!Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "ISOMetricFineProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetricfine_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetricfine_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            // thread class and direction are only sensible if threaded
            // fit only sensible if not threaded
            ThreadFit.setReadOnly(Threaded.getValue());
            ThreadClass.setReadOnly(!Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "UNC") {
            ThreadClass.setEnums(ThreadClass_UNC_Enums);
            HoleCutType.setEnums(HoleCutType_UNC_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            // thread class and direction are only sensible if threaded
            // fit only sensible if not threaded
            ThreadFit.setReadOnly(Threaded.getValue());
            ThreadClass.setReadOnly(!Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "UNF") {
            ThreadClass.setEnums(ThreadClass_UNF_Enums);
            HoleCutType.setEnums(HoleCutType_UNF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            // thread class and direction are only sensible if threaded
            // fit only sensible if not threaded
            ThreadFit.setReadOnly(Threaded.getValue());
            ThreadClass.setReadOnly(!Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "UNEF") {
            ThreadClass.setEnums(ThreadClass_UNEF_Enums);
            HoleCutType.setEnums(HoleCutType_UNEF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            // thread class and direction are only sensible if threaded
            // fit only sensible if not threaded
            ThreadFit.setReadOnly(Threaded.getValue());
            ThreadClass.setReadOnly(!Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "BSP") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_BSP_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            ThreadFit.setReadOnly(Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "NPT") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_NPT_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            ThreadFit.setReadOnly(Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "BSW") {
            ThreadClass.setEnums(ThreadClass_BSW_Enums);
            HoleCutType.setEnums(HoleCutType_BSW_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            ThreadFit.setReadOnly(Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "BSF") {
            ThreadClass.setEnums(ThreadClass_BSF_Enums);
            HoleCutType.setEnums(HoleCutType_BSF_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            ThreadFit.setReadOnly(Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }
        else if (type == "ISOTyre") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
            Threaded.setReadOnly(false);
            ThreadSize.setReadOnly(false);
            ThreadFit.setReadOnly(Threaded.getValue());
            Diameter.setReadOnly(true);
            ModelThread.setReadOnly(!Threaded.getValue());
            UseCustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue());
            CustomThreadClearance.setReadOnly(!Threaded.getValue() || !ModelThread.getValue() || !UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(!Threaded.getValue());
            ThreadDepth.setReadOnly(!Threaded.getValue());
        }

        if (holeCutTypeStr == "None") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(true);
            HoleCutDepth.setReadOnly(true);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Counterbore") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else { // screw definition
            HoleCutCustomValues.setReadOnly(false);
            if (HoleCutCustomValues.getValue()) {
                HoleCutDiameter.setReadOnly(false);
                HoleCutDepth.setReadOnly(false);
                // we must not set HoleCutCountersinkAngle here because the info if this can
                // be enabled is first available in updateHoleCutParams and thus handled there
                updateHoleCutParams();
            }
            else {
                HoleCutDiameter.setReadOnly(true);
                HoleCutDepth.setReadOnly(true);
                HoleCutCountersinkAngle.setReadOnly(true);
            }
        }

        // Every type above states ModelThread's; the cosmetic thread goes
        // with it, whatever the type
        CosmeticThread.setReadOnly(type == "None" || !Threaded.getValue());

        // Signal changes to these
        ProfileBased::onChanged(&ThreadSize);
        ProfileBased::onChanged(&ThreadClass);
        ProfileBased::onChanged(&HoleCutType);
        ProfileBased::onChanged(&Threaded);

        // Diameter parameter depends on this
        if (type != "None")
            updateDiameterParam();
    }
    else if (prop == &Threaded) {
        std::string type(ThreadType.getValueAsString());

        // thread class and direction are only sensible if threaded
        // fit only sensible if not threaded
        if (Threaded.getValue()) {
            ThreadClass.setReadOnly(false);
            ThreadDirection.setReadOnly(false);
            ThreadFit.setReadOnly(true);
            ModelThread.setReadOnly(false);
            CosmeticThread.setReadOnly(false);
            UseCustomThreadClearance.setReadOnly(false);
            CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(false);
            ThreadDepth.setReadOnly(std::string(ThreadDepthType.getValueAsString()) != "Dimension");
            if (Tapered.getValue() && TaperedAngle.getValue() == 90)
                TaperedAngle.setValue(getThreadProfileAngle());
        }
        else {
            ThreadClass.setReadOnly(true);
            ThreadDirection.setReadOnly(true);
            if (type == "None")
                ThreadFit.setReadOnly(true);
            else
                ThreadFit.setReadOnly(false);
            ModelThread.setReadOnly(true);
            // Read only, but left as it was: upstream clears it here, and
            // then ticking Threaded again brings the hole back with no
            // thread drawn, which is not what anyone ticking it meant
            CosmeticThread.setReadOnly(true);
            UseCustomThreadClearance.setReadOnly(true);
            CustomThreadClearance.setReadOnly(true);
            ThreadDepthType.setReadOnly(true);
            ThreadDepth.setReadOnly(true);
        }

        // Diameter parameter depends on this
        updateDiameterParam();
    }
    else if (prop == &ModelThread) {
        // Diameter parameter depends on this
        updateDiameterParam();
        UseCustomThreadClearance.setReadOnly(!ModelThread.getValue());
        // A modelled thread is the thread; drawing one over it would be
        // two. Kept exclusive in the stored values too, as upstream keeps
        // them, because upstream's own restore enforces it the other way
        // round and would drop the modelled one from a file holding both.
        if (ModelThread.getValue() && CosmeticThread.getValue())
            CosmeticThread.setValue(false);
    }
    else if (prop == &CosmeticThread) {
        if (CosmeticThread.getValue() && ModelThread.getValue())
            ModelThread.setValue(false);
    }
    else if (prop == &DrillPoint) {
        if (DrillPoint.getValue() == 1) {
            DrillPointAngle.setReadOnly(false);
            DrillForDepth.setReadOnly(false);
        }
        else {
            DrillPointAngle.setReadOnly(true);
            DrillForDepth.setReadOnly(true);
        }
    }
    else if (prop == &Tapered) {
        if (Tapered.getValue()) {
            TaperedAngle.setReadOnly(false);
            if (Threaded.getValue() && TaperedAngle.getValue() == 90)
                TaperedAngle.setValue(getThreadProfileAngle());
        }
        else {
            TaperedAngle.setValue(90);
            TaperedAngle.setReadOnly(true);
        }
    }
    else if (prop == &ThreadSize) {
        updateDiameterParam();
        if (!isRestoring())
            updateThreadDepthParam();
        // updateHoleCutParams() will later automatically be called because
    }
    else if (prop == &ThreadFit) {
        updateDiameterParam();
    }
    else if (prop == &Diameter) {
        // a changed diameter means we also need to check the hole cut
        // because the hole cut diameter must not be <= than the diameter
        updateHoleCutParams();
        if (ThreadType.getValue() == 0) {
            // Profile is None but this is needed to find the closest
            // designation if the user switch to threaded
            ThreadDiameter.setValue(Diameter.getValue());
            threadPitch = 0.0;
        }
    }
    else if (prop == &HoleCutType) {
        ProfileBased::onChanged(&HoleCutDiameter);
        ProfileBased::onChanged(&HoleCutDepth);
        ProfileBased::onChanged(&HoleCutCountersinkAngle);

        // the read-only states are set in updateHoleCutParams()
        updateHoleCutParams();
    }
    else if (prop == &HoleCutCustomValues) {
        // when going back to standardized values, we must recalculate
        // also to find out if HoleCutCountersinkAngle can be ReadOnly
        // both an also the read-only states is done in updateHoleCutParams()
        updateHoleCutParams();
    }
    else if (prop == &DepthType) {
        std::string DepthMode(DepthType.getValueAsString());
        Depth.setReadOnly(DepthMode != "Dimension");
        DrillPoint.setReadOnly(DepthMode != "Dimension");
        DrillPointAngle.setReadOnly(DepthMode != "Dimension");
        DrillForDepth.setReadOnly(DepthMode != "Dimension");
        if (!isRestoring()) {
            if (DepthMode != "Dimension") {
                // if through all, set the depth accordingly; the thread depth
                // follows in updateThreadDepthParam() unless its own type
                // keeps a dimension
                Depth.setValue(getThroughAllLength());
            }
            updateThreadDepthParam();
        }
    }
    else if (prop == &Depth) {
        if (!isRestoring()) {
            // the depth cannot be greater than the through-all length
            if (Depth.getValue() > getThroughAllLength())
                Depth.setValue(getThroughAllLength());
        }

        if (std::string(ThreadDepthType.getValueAsString()) != "Dimension")
            updateDiameterParam();  // make sure diameter and pitch are updated.

        if (!isRestoring())
            updateThreadDepthParam();
    }
    else if (prop == &ThreadDepthType) {
        if (!isRestoring())
            updateThreadDepthParam();
        ThreadDepth.setReadOnly(Threaded.getValue()
            && std::string(ThreadDepthType.getValueAsString()) != "Dimension");
    }
    else if (prop == &ThreadDepth) {
        // the thread depth cannot be greater than the hole depth
        if (ThreadDepth.getValue() > Depth.getValue())
            ThreadDepth.setValue(Depth.getValue());
    }
    else if (prop == &UseCustomThreadClearance) {
        updateDiameterParam();
        CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
    }
    else if (prop == &CustomThreadClearance) {
        updateDiameterParam();
    }

    ProfileBased::onChanged(prop);
}

/**
  * Computes 2D intersection between the lines (pa1, pa2) and (pb1, pb2).
  * The lines are assumed to be crossing, and it is an error
  * to specify parallel lines.
  * Only the x and y coordinates of the points are used to compute the 2D intersection.
  *
  * The result are the x and y coordinate of the intersection point.
  */
static void computeIntersection(gp_Pnt pa1, gp_Pnt pa2, gp_Pnt pb1, gp_Pnt pb2, double& x, double& y)
{
    double vx1 = pa1.X() - pa2.X();
    double vy1 = pa1.Y() - pa2.Y();
    double vx2 = pb1.X() - pb2.X();
    double vy2 = pb1.Y() - pb2.Y();
    double x1 = pa1.X();
    double y1 = pa1.Y();
    double x2 = pb1.X();
    double y2 = pb1.Y();

    /* Solve the system
        x1 + t1 * vx1 = x2 + t2 * vx2
        y1 + t1 * vy1 = y2 + t2 * vy2

        =>

       [ vx1    -vx2 ] [ t1 ] = [ x2 - x1 ]
       [ vy1    -vy2 ] [ t2 ] = [ y2 - y1 ]

       =>

       [ t1 ] = f * [ -vy2  vx2 ] [ x2 - x1 ]
       [ t2 ] =     [ -vy1  vx1 ] [ y2 - y1 ]

     */

    assert(((vx1 * -vy2) - (-vx2 * vy1)) != 0);

    double f = 1 / ((vx1 * -vy2) - (-vx2 * vy1));

    double t1 = -vy2 * f * (x2 - x1) + vx2 * f * (y2 - y1);

#ifdef _DEBUG
    double t2 = -vy1 * f * (x2 - x1) + vx1 * f * (y2 - y1);

    assert((x1 + t1 * vx1) - (x2 + t2 * vx2) < 1e-6);
    assert((y1 + t1 * vy1) - (y2 + t2 * vy2) < 1e-6);
#endif

    x = x1 + t1 * vx1;
    y = y1 + t1 * vy1;
}

short Hole::mustExecute() const
{
    if (ThreadType.isTouched() ||
        Threaded.isTouched() ||
        ThreadSize.isTouched() ||
        ThreadClass.isTouched() ||
        ThreadFit.isTouched() ||
        Diameter.isTouched() ||
        ThreadDirection.isTouched() ||
        HoleCutType.isTouched() ||
        HoleCutDiameter.isTouched() ||
        HoleCutDepth.isTouched() ||
        HoleCutCountersinkAngle.isTouched() ||
        DepthType.isTouched() ||
        Depth.isTouched() ||
        DrillPoint.isTouched() ||
        DrillPointAngle.isTouched() ||
        Tapered.isTouched() ||
        TaperedAngle.isTouched() ||
        ModelThread.isTouched() ||
        UseCustomThreadClearance.isTouched() ||
        CustomThreadClearance.isTouched() ||
        ThreadDepthType.isTouched() ||
        ThreadDepth.isTouched() ||
        BaseProfileType.isTouched() ||
        StartType.isTouched() ||
        StartOffset.isTouched() ||
        StartReference.isTouched()
        )
        return 1;
    return ProfileBased::mustExecute();
}

void Hole::onDocumentRestored()
{
    ProfileBased::onDocumentRestored();
    updateProps();
}

void Hole::updateProps()
{
    onChanged(&Threaded);
    onChanged(&ThreadType);
    onChanged(&ThreadSize);
    onChanged(&ThreadClass);
    onChanged(&ThreadFit);
    onChanged(&Diameter);
    onChanged(&ThreadDirection);
    onChanged(&HoleCutType);
    onChanged(&HoleCutDiameter);
    onChanged(&HoleCutDepth);
    onChanged(&HoleCutCountersinkAngle);
    onChanged(&DepthType);
    onChanged(&Depth);
    onChanged(&DrillPoint);
    onChanged(&DrillPointAngle);
    onChanged(&Tapered);
    onChanged(&TaperedAngle);
    onChanged(&ModelThread);
    onChanged(&UseCustomThreadClearance);
    onChanged(&CustomThreadClearance);
    onChanged(&ThreadDepthType);
    onChanged(&ThreadDepth);
    onChanged(&StartType);
}

static gp_Pnt toPnt(gp_Vec dir)
{
    return {dir.X(), dir.Y(), dir.Z()};
}

App::DocumentObjectExecReturn* Hole::execute()
{
    // The profile as it is, not a face made of it: only its circles, arcs
    // and points are used, and a face has no points
    TopoShape profileshape;
    try {
        profileshape = getProfileShape();
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    // Find the base shape
    TopoShape base;
    try {
        if (!NewSolid.getValue())
            base = getBaseShape();
    }
    catch (const Base::Exception&) {
        std::string text(QT_TRANSLATE_NOOP("Exception", "The requested feature cannot be created. The reason may be that:\n"
            "  - the active Body does not contain a base shape, so there is no\n"
            "  material to be removed;\n"
            "  - the selected sketch does not belong to the active Body."));
        return new App::DocumentObjectExecReturn(text);
    }

    try {
        // A negative custom clearance can take the diameter below its range
        if (Diameter.getValue() < diameterRange.LowerBound) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Diameter too small"));
        }
        std::string method(DepthType.getValueAsString());
        double length = 0.0;

        auto invObjLoc = this->positionByPrevious();

        base.move(invObjLoc);

        if (profileshape.isNull())
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Creating a face from sketch failed"));

        /* Build the prototype hole */

        // Get vector normal to profile, in global coordinates
        Base::Vector3d SketchVector = guessNormalDirection(profileshape);
        profileshape.move(invObjLoc);
        if (Reversed.getValue())
            SketchVector *= -1.0;

        // Define this as zDir
        gp_Vec zDir(SketchVector.x, SketchVector.y, SketchVector.z);
        zDir.Transform(invObjLoc.Transformation());
        gp_Vec xDir = computePerpendicular(zDir);

        // The holes go down -zDir; they start where StartType says
        const gp_Dir holeDirection(-zDir);
        profileshape = moveProfileToStart(profileshape, holeDirection,
                                          startOffset(profileshape, holeDirection, invObjLoc));

        if (method == "Dimension")
            length = Depth.getValue();
        else if (method == "UpToFirst") {
            /* TODO */
        }
        else if (method == "ThroughAll") {
            length = getThroughAllLength();
        }
        else
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Unsupported length specification"));

        if (length <= 0.0)
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid hole depth"));

        BRepBuilderAPI_MakeWire mkWire;
        const std::string holeCutType = HoleCutType.getValueAsString();
        const std::string threadType = ThreadType.getValueAsString();
        bool isCountersink = (holeCutType == "Countersink" ||
            isDynamicCountersink(threadType, holeCutType));
        bool isCounterbore = (holeCutType == "Counterbore" ||
            isDynamicCounterbore(threadType, holeCutType));
        bool isCounterdrill = (holeCutType == "Counterdrill");

        double TaperedAngleVal =
            Tapered.getValue() ? Base::toRadians(TaperedAngle.getValue()) : Base::toRadians(90.0);
        double radiusBottom = Diameter.getValue() / 2.0 - length / tan(TaperedAngleVal);

        double radius = Diameter.getValue() / 2.0;
        gp_Pnt firstPoint(0, 0, 0);
        gp_Pnt lastPoint(0, 0, 0);
        double lengthCounter = 0.0;
        double xPosCounter = 0.0;
        double zPosCounter = 0.0;

        if (TaperedAngleVal <= 0.0 || TaperedAngleVal > Base::toRadians(180.0))
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid taper angle"));

        if (isCountersink || isCounterbore || isCounterdrill) {
            double holeCutRadius = HoleCutDiameter.getValue() / 2.0;
            double holeCutDepth = HoleCutDepth.getValue();
            double countersinkAngle = Base::toRadians(HoleCutCountersinkAngle.getValue() / 2.0);

            if (isCounterbore) {
                // Counterbore is rendered the same way as a countersink, but with a hardcoded
                // angle of 90deg
                countersinkAngle = Base::toRadians(90.0);
            }

            if (isCountersink) {
                holeCutDepth = 0.0;
                // We cannot recalculate the HoleCutDiameter because the previous HoleCutDepth
                // is unknown. Therefore we cannot know with what HoleCutDepth the current
                // HoleCutDiameter was calculated.
            }

            if (holeCutRadius < radius)
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Hole cut diameter too small"));

            if (holeCutDepth > length)
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Hole cut depth must be less than hole depth"));

            if (holeCutDepth < 0.0)
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Hole cut depth must be greater or equal to zero"));

            // Top point
            gp_Pnt newPoint = toPnt(holeCutRadius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            // Bottom of counterbore
            if (holeCutDepth > 0.0) {
                newPoint = toPnt(holeCutRadius * xDir - holeCutDepth * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }

            // Compute intersection of tapered edge and line at bottom of counterbore hole
            computeIntersection(gp_Pnt(holeCutRadius, -holeCutDepth, 0 ),
                                gp_Pnt(holeCutRadius - sin(countersinkAngle),
                                       -cos(countersinkAngle) - holeCutDepth, 0),
                                gp_Pnt(radius, 0, 0),
                                gp_Pnt(radiusBottom, -length, 0), xPosCounter, zPosCounter);

            if (-length > zPosCounter)
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid countersink"));

            lengthCounter = zPosCounter;
            newPoint = toPnt(xPosCounter * xDir + zPosCounter * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else {
            gp_Pnt newPoint = toPnt(radius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
            lengthCounter = 0.0;
        }

        std::string drillPoint = DrillPoint.getValueAsString();
        double xPosDrill = 0.0;
        double zPosDrill = 0.0;
        if (drillPoint == "Flat") {
            gp_Pnt newPoint = toPnt(radiusBottom * xDir + -length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            newPoint = toPnt(-length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else if (drillPoint == "Angled") {
            double drillPointAngle = Base::toRadians((180.0 - DrillPointAngle.getValue()) / 2.0);
            gp_Pnt newPoint;
            bool isDrillForDepth = DrillForDepth.getValue();

            // the angle is in any case > 0 and < 90 but nevertheless this safeguard:
            if (drillPointAngle <= 0.0 || drillPointAngle >= Base::toRadians(180.0))
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point angle"));

            // if option to take drill point size into account
            // the next wire point is the intersection of the drill edge and the hole edge
            if (isDrillForDepth) {
                computeIntersection(gp_Pnt(0, -length, 0),
                    gp_Pnt(radius, radius * tan(drillPointAngle) - length, 0),
                    gp_Pnt(radius, 0, 0),
                    gp_Pnt(radiusBottom, -length, 0), xPosDrill, zPosDrill);
                if (zPosDrill > 0 || zPosDrill >= lengthCounter)
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point"));

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                newPoint = toPnt(-length * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
            else {
                xPosDrill = radiusBottom;
                zPosDrill = -length;

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                // the end point is the size of the drill tip
                newPoint = toPnt((-length - radius * tan(drillPointAngle)) * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
        }

        mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, firstPoint));

        TopoDS_Wire wire = mkWire.Wire();

        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

        double angle = Base::toRadians<double>(360.0);
        BRepPrimAPI_MakeRevol RevolMaker(face, gp_Ax1(firstPoint, zDir), angle);
        if (!RevolMaker.IsDone())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Could not revolve sketch"));

        TopoDS_Shape protoHole = RevolMaker.Shape();
        if (protoHole.IsNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: Resulting shape is empty"));


        // Make thread
        if (Threaded.getValue() && ModelThread.getValue()) {
            TopoDS_Shape protoThread = makeThread(xDir, zDir, length);

            // fuse the thread to the hole
            BRepAlgoAPI_Fuse mkFuse(protoHole, protoThread);
            if (!mkFuse.IsDone())
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Error: Adding the thread failed"));

            // we reuse the name protoHole (only now it is threaded)
            protoHole = mkFuse.Shape();
        }

        std::vector<TopoShape> holes;
        auto compound = findHoles(holes, profileshape, protoHole);
        // Nothing to drill: the empty tool used to reach makeBoolean(), which
        // hands back the tool when it is not a solid, and the body's shape
        // went empty without a word (upstream 7a672a3207).
        if (holes.empty())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Hole error: the profile has no circle, arc or point to center a hole on, of those BaseProfileType takes"));

        TopoShape result(0,getDocument()->getStringHasher());

        // set the subtractive shape property for later usage in e.g. pattern
        this->AddSubShape.setValue(compound);
        if (isRecomputePaused())
            return App::DocumentObject::StdReturn;

        // First try cuting with compound which will be faster as it is done in
        // parallel
        bool retry = true;
        try {
            this->Shape.setValue(makeBoolean(base, compound));
            retry = false;
        } catch (Standard_Failure & e) {
            FC_WARN(getFullName() << ": boolean operation with compound failed ("
                    << e.GetMessageString() << "), retry...");
        } catch (Base::Exception & e)  {
            FC_WARN(getFullName() << ": boolean operation with compound failed ("
                    << e.what() << "), retry...");
        }

        if (retry) {
            int i = 0;
            const char *maker;
            switch (getAddSubType()) {
            case Subtractive:
                maker = Part::OpCodes::Cut;
                break;
            case Intersecting:
                maker = Part::OpCodes::Common;
                break;
            default:
                maker = Part::OpCodes::Fuse;
            }
            for (auto & hole : holes) {
                ++i;
                try {
                    hole.Tag = -this->getID();
                    result.makEBoolean(maker, {base,hole});
                } catch (Standard_Failure &) {
                    std::string msg(QT_TRANSLATE_NOOP("Exception", "Boolean operation failed on profile Edge"));
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                } catch (Base::Exception &e) {
                    e.ReportException();
                    std::string msg(QT_TRANSLATE_NOOP("Exception", "Boolean operataion failed on profile Edge"));
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
                base = getSolid(result);
                if (base.isNull()) {
                    std::string msg(QT_TRANSLATE_NOOP("Exception", "Boolean operataion produced non-solid on profile Edge"));
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
            }
            result = base;
            result = refineShapeIfActive(result);
            this->Shape.setValue(result);
        }

        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        if (std::string(e.GetMessageString()) == "TopoDS::Face"
            && (std::string(DepthType.getValueAsString()) == "UpToFirst"
                || std::string(DepthType.getValueAsString()) == "UpToFace"))
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                "Could not create face from sketch.\n"
                "Intersecting sketch entities or multiple faces in a sketch are not allowed "
                "for making a pocket up to a face."));
        else
            return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
}

void Hole::rotateToNormal(const gp_Dir& helixAxis, const gp_Dir& normalAxis,
                          TopoDS_Shape& helixShape) const
{
    auto getRotationAxis = [](const gp_Dir& dir1, const gp_Dir& dir2, gp_Dir& dir3, double& angle) {
        if (dir1.IsEqual(dir2, Precision::Angular()))
            return false;

        angle = acos(dir1 * dir2);
        if (dir1.IsOpposite(dir2, Precision::Angular())) {
            // Create a vector that is not parallel to dir1
            gp_XYZ xyz(dir1.XYZ());
            if (fabs(xyz.X()) <= fabs(xyz.Y()) && fabs(xyz.X()) <= fabs(xyz.Z()))
                xyz.SetX(1.0);
            else if (fabs(xyz.Y()) <= fabs(xyz.X()) && fabs(xyz.Y()) <= fabs(xyz.Z()))
                xyz.SetY(1.0);
            else
                xyz.SetZ(1.0);
            dir3 = dir1.Crossed(gp_Dir(xyz));
        }
        else {
            dir3 = dir1.Crossed(dir2);
        }
        return true;
    };
    // rotate the helixAxis so that it is pointing in the normalAxis.
    double angle;
    gp_Dir rotAxis;
    if (getRotationAxis(helixAxis, normalAxis, rotAxis, angle)) {
        gp_Pnt origo(0.0, 0.0, 0.0);
        gp_Trsf mov = helixShape.Location().Transformation();
        mov.SetRotation(gp_Ax1(origo, rotAxis), angle);
        TopLoc_Location loc2(mov);
        helixShape.Move(loc2);
    }
}

gp_Vec Hole::computePerpendicular(const gp_Vec& zDir) const
{
    // Define xDir
    gp_Vec xDir;

    /* Compute xDir normal to zDir */
    if (std::abs(zDir.Z() - zDir.X()) > Precision::Confusion())
        xDir = gp_Vec(zDir.Z(), 0, -zDir.X());
    else if (std::abs(zDir.Z() - zDir.Y()) > Precision::Confusion())
        xDir = gp_Vec(zDir.Y(), -zDir.X(), 0);
    else
        xDir = gp_Vec(0, -zDir.Z(), zDir.Y());

    // Normalize xDir; this is needed as the computation above does not necessarily give
    // a unit-length vector.
    xDir.Normalize();
    return xDir;
}

Base::Vector3d Hole::guessNormalDirection(const TopoShape& profileshape) const
{
    // A hole made on a cylindrical face (an existing hole, say) is drilled
    // along the cylinder's axis. getProfileNormal() would take the normal at
    // the middle of the face, which is radial.
    if (profileshape.hasSubShape(TopAbs_FACE)) {
        BRepAdaptor_Surface sf(TopoDS::Face(profileshape.getSubShape(TopAbs_FACE, 1)));
        if (sf.GetType() == GeomAbs_Cylinder) {
            const gp_Dir& dir = sf.Cylinder().Axis().Direction();
            return Base::Vector3d(dir.X(), dir.Y(), dir.Z());
        }
    }
    return getProfileNormal();
}

void Hole::forEachHoleCenter(
    const TopoShape& profileshape,
    const std::function<void(const TopoShape&, const gp_Pnt&)>& fn) const
{
    const int types = BaseProfileType.getValue();
    if (types & (OnCircles | OnArcs)) {
        for(const auto &profileEdge : profileshape.getSubTopoShapes(TopAbs_EDGE)) {
            BRepAdaptor_Curve adaptor(TopoDS::Edge(profileEdge.getShape()));
            if (adaptor.GetType() != GeomAbs_Circle)
                continue;
            if (!(types & (adaptor.IsClosed() ? OnCircles : OnArcs)))
                continue;
            fn(profileEdge, adaptor.Circle().Location());
        }
    }
    // Points on their own, not the ends of the edges
    if (types & OnPoints) {
        for (const auto &vertex : profileshape.getSubTopoShapes(TopAbs_VERTEX, TopAbs_EDGE))
            fn(vertex, BRep_Tool::Pnt(TopoDS::Vertex(vertex.getShape())));
    }
}

std::vector<Hole::CosmeticThreadBore> Hole::getCosmeticThreads() const
{
    std::vector<CosmeticThreadBore> bores;
    if (!Threaded.getValue() || !CosmeticThread.getValue() || ModelThread.getValue()
            || Suppress.getValue())
        return bores;

    const int type = ThreadType.getValue();
    const int size = ThreadSize.getValue();
    if (type <= 0 || size < 0)
        return bores;
    const double pitch = threadDescription[type][size].pitch;
    if (!(pitch > 0.0))
        return bores;

    try {
        TopoShape profileshape = getProfileShape();
        if (profileshape.isNull())
            return bores;

        // The frame execute() drills in: the feature's own shape
        // coordinates, its Placement taken off (execute() gets there
        // through positionByPrevious(), which has already set the
        // Placement this inverts)
        const TopLoc_Location invObjLoc = getLocation().Inverted();
        Base::Vector3d normal = guessNormalDirection(profileshape);
        profileshape.move(invObjLoc);
        if (Reversed.getValue())
            normal *= -1.0;
        gp_Vec zDir(normal.x, normal.y, normal.z);
        zDir.Transform(invObjLoc.Transformation());
        if (zDir.Magnitude() < Precision::Confusion())
            return bores;
        zDir.Normalize();
        // Where execute() starts them
        const gp_Dir holeDirection(-zDir);
        profileshape = moveProfileToStart(profileshape, holeDirection,
                                          startOffset(profileshape, holeDirection, invObjLoc));

        const std::string depthType = DepthType.getValueAsString();
        const double holeLength =
            depthType == "ThroughAll" ? getThroughAllLength() : Depth.getValue();
        const std::string threadDepthType = ThreadDepthType.getValueAsString();
        const double length = threadDepthType == "Hole Depth"
            ? holeLength : std::min(ThreadDepth.getValue(), holeLength);
        if (!(length > 0.0))
            return bores;

        CosmeticThreadBore bore;
        // The hole goes down -zDir from the profile
        bore.direction = Base::Vector3d(-zDir.X(), -zDir.Y(), -zDir.Z());
        bore.radius = Diameter.getValue() / 2.0;
        bore.taper = Tapered.getValue()
            ? Base::toRadians<double>(90.0 - TaperedAngle.getValue()) : 0.0;
        bore.length = length;
        bore.pitch = pitch;
        // Whitworth and the pipe threads derived from it are 55 degrees
        // with a rounded 0.640P form; ISO, Unified and NPT are 60 degrees
        // with the 5/8 H = 0.541P of a truncated one
        const std::string threadType = ThreadType.getValueAsString();
        const bool whitworth =
            threadType == "BSW" || threadType == "BSF" || threadType == "BSP";
        bore.profileAngle = whitworth ? 55.0 : 60.0;
        bore.height = (whitworth ? 0.640327 : 0.541266) * pitch;
        bore.leftHand = std::string(ThreadDirection.getValueAsString()) == "Left";

        forEachHoleCenter(profileshape, [&](const TopoShape&, const gp_Pnt& loc) {
            bore.origin = Base::Vector3d(loc.X(), loc.Y(), loc.Z());
            bores.push_back(bore);
        });
    }
    catch (const Base::Exception&) {
        bores.clear();
    }
    catch (const Standard_Failure&) {
        bores.clear();
    }
    return bores;
}

TopoShape Hole::findHoles(std::vector<TopoShape> &holes,
                          const TopoShape& profileshape,
                          const TopoDS_Shape& protoHole) const
{
    auto addHole = [&](const TopoShape &source, const gp_Pnt &loc) {
        gp_Trsf localSketchTransformation;
        localSketchTransformation.SetTranslation( gp_Pnt( 0, 0, 0 ),
                                                    gp_Pnt(loc.X(), loc.Y(), loc.Z()) );

        Part::ShapeMapper mapper;
        mapper.populate(true, source, TopoShape(protoHole).getSubTopoShapes(TopAbs_FACE));

        TopoShape hole(-getID(), getDocument()->getStringHasher());
        hole.makESHAPE(protoHole, mapper, {source});

        // transform and generate element map.
        hole = hole.makETransform(localSketchTransformation);
        holes.push_back(hole);
    };

    forEachHoleCenter(profileshape, addHole);
    return TopoShape().makECompound(holes);
}

TopoDS_Shape Hole::makeThread(const gp_Vec& xDir, const gp_Vec& zDir, double length)
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        THROWM(Base::IndexError, QT_TRANSLATE_NOOP("Exception", "Thread type out of range"))
    }
    if (threadSize < 0) {
        THROWM(Base::IndexError, QT_TRANSLATE_NOOP("Exception", "Thread size out of range"))
    }

    bool leftHanded = (bool)ThreadDirection.getValue();

    // Nomenclature and formulae according to Figure 1 of ISO 68-1
    // this is the same for all metric and UTS threads as stated here:
    // https://en.wikipedia.org/wiki/File:ISO_and_UTS_Thread_Dimensions.svg
    // Note that in the ISO standard, Dmaj is called D.
    double Dmaj = threadDescription[threadType][threadSize].diameter / 2; // major diameter position
    double Pitch = getThreadPitch();

    double clearance; // clearance to be added on the diameter
    if (UseCustomThreadClearance.getValue())
        clearance = CustomThreadClearance.getValue() / 2;
    else
        clearance = getThreadClassClearance() / 2;

    BRepBuilderAPI_MakeWire mkThreadWire;
    double H;
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "BSP" || threadTypeStr == "BSW" || threadTypeStr == "BSF") {
        H = 0.960491 * Pitch; // Height of Sharp V
        double radius = 0.137329 * Pitch; // radius of the crest
        double h = 0.640627 * Pitch; // height of the thread
        // construct the cross section going counter-clockwise

        gp_Pnt p1 = toPnt((Dmaj - h + clearance) * xDir + Pitch / 8 * zDir);
        gp_Pnt p4 = toPnt((Dmaj - h + clearance) * xDir + 7 * Pitch / 8 * zDir);
        gp_Pnt p5 = toPnt(0.9 * (Dmaj - h) * xDir + 7 * Pitch / 8 * zDir);
        gp_Pnt p6 = toPnt(0.9 * (Dmaj - h) * xDir + Pitch / 8 * zDir);

        // Calculate positions for p2 and p3 based on the arc radius
        double p23x = Dmaj + clearance - radius * (1 - std::cos(M_PI / 4));

        gp_Pnt p2 = toPnt(p23x * xDir + 7 * Pitch / 16 * zDir);
        gp_Pnt p3 = toPnt(p23x * xDir + 9 * Pitch / 16 * zDir);
        gp_Pnt crest = toPnt((Dmaj + clearance) * xDir + Pitch / 2 * zDir);

        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        Handle(Geom_TrimmedCurve) arc1 = GC_MakeArcOfCircle(p2, crest, p3).Value();
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(arc1).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p4, p5).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p5, p6).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p6, p1).Edge());
    } else {
        H = sqrt(3) / 2 * Pitch; // height of fundamental triangle
        double h = 5 * H / 8; // height of the thread
        // construct the cross section going counter-clockwise
        // for graphical explanation of geometrical construction of p1-p6 see:
        // https://forum.freecad.org/viewtopic.php?f=19&t=54284#p466570
        gp_Pnt p1 = toPnt((Dmaj - h + clearance) * xDir + Pitch / 8 * zDir);
        gp_Pnt p2 = toPnt((Dmaj + clearance) * xDir + 7 * Pitch / 16 * zDir);
        gp_Pnt p3 = toPnt((Dmaj + clearance) * xDir + 9 * Pitch / 16 * zDir);
        gp_Pnt p4 = toPnt((Dmaj - h + clearance) * xDir + 7 * Pitch / 8 * zDir);
        gp_Pnt p5 = toPnt(0.9 * (Dmaj - h) * xDir + 7 * Pitch / 8 * zDir);
        gp_Pnt p6 = toPnt(0.9 * (Dmaj - h) * xDir + Pitch / 8 * zDir);

        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        if (threadTypeStr == "ISOTyre") {
            // ISO 4570 rounds the crest (upstream 551c15b48f)
            gp_Pnt crest = toPnt((Dmaj + clearance + Pitch / 32) * xDir + Pitch / 2 * zDir);
            Handle(Geom_TrimmedCurve) arc1 = GC_MakeArcOfCircle(p2, crest, p3).Value();
            mkThreadWire.Add(BRepBuilderAPI_MakeEdge(arc1).Edge());
        }
        else {
            mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p2, p3).Edge());
        }
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p4, p5).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p5, p6).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p6, p1).Edge());
    }

    mkThreadWire.Build();
    TopoDS_Wire threadWire = mkThreadWire.Wire();

    //create the helix path
    double threadDepth = ThreadDepth.getValue();
    double helixLength = threadDepth + Pitch / 2;
    double holeDepth = Depth.getValue();
    std::string threadDepthMethod(ThreadDepthType.getValueAsString());
    std::string depthMethod(DepthType.getValueAsString());
    if (threadDepthMethod != "Dimension") {
        if (depthMethod == "ThroughAll") {
            threadDepth = length;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + 2 * Pitch;
        }
        else if (threadDepthMethod == "Tapped (DIN76)") {
            threadDepth = holeDepth - getThreadRunout();
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 2;
        }
        else { // Hole depth
            threadDepth = holeDepth;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 8;
        }
    }
    else {
        if (depthMethod == "Dimension") {
            // the thread must not be deeper than the hole
            // thus the max helixLength is holeDepth + P / 8;
            if (threadDepth > (holeDepth - Pitch / 2))
                helixLength = holeDepth + Pitch / 8;
        }
    }
    TopoDS_Shape helix = TopoShape().makeLongHelix(Pitch, helixLength, Dmaj, 0.0, leftHanded);

    gp_Pnt origo(0.0, 0.0, 0.0);
    gp_Dir dir_axis1(0.0, 0.0, 1.0);  // pointing along the helix axis, as created.
    gp_Dir dir_axis2(1.0, 0.0, 0.0);  // pointing towards the helix start point, as created.

    // Reverse the direction of the helix. So that it goes into the material
    gp_Trsf mov;
    mov.SetRotation(gp_Ax1(origo, dir_axis2), M_PI);
    TopLoc_Location loc1(mov);
    helix.Move(loc1);

    // rotate the helix so that it is pointing in the zdir.
    rotateToNormal(dir_axis1, zDir, helix);

    // create the pipe shell
    BRepOffsetAPI_MakePipeShell mkPS(TopoDS::Wire(helix));
    mkPS.SetTolerance(Precision::Confusion());
    mkPS.SetTransitionMode(BRepBuilderAPI_Transformed);
    mkPS.SetMode(true);  //This is for frenet
    mkPS.Add(threadWire);
    if (!mkPS.IsReady())
        THROWM(Base::CADKernelError, QT_TRANSLATE_NOOP("Exception", "Error: Thread could not be built"))
    TopoDS_Shape shell = mkPS.Shape();

    // create faces at the ends of the pipe shell
    TopTools_ListOfShape sim;
    mkPS.Simulate(2, sim);
    std::vector<TopoDS_Wire> frontwires, backwires;
    frontwires.push_back(TopoDS::Wire(sim.First()));
    backwires.push_back(TopoDS::Wire(sim.Last()));
    // build the end faces
    TopoDS_Shape front = Part::FaceMakerCheese::makeFace(frontwires);
    TopoDS_Shape back = Part::FaceMakerCheese::makeFace(backwires);

    // sew the shell and end faces
    BRepBuilderAPI_Sewing sewer;
    sewer.SetTolerance(Precision::Confusion());
    sewer.Add(front);
    sewer.Add(back);
    sewer.Add(shell);
    sewer.Perform();

    // make the closed off shell into a solid
    BRepBuilderAPI_MakeSolid mkSolid;
    mkSolid.Add(TopoDS::Shell(sewer.SewedShape()));
    if (!mkSolid.IsDone())
        THROWM(Base::CADKernelError, QT_TRANSLATE_NOOP("Exception", "Error: Result is not a solid"))
    TopoDS_Shape result = mkSolid.Shape();

    // check if the algorithm has confused the inside and outside of the solid
    BRepClass3d_SolidClassifier SC(result);
    SC.PerformInfinitePoint(Precision::Confusion());
    if (SC.State() == TopAbs_IN)
        result.Reverse();

    // we are done
    return result;
}

void Hole::addCutType(const CutDimensionSet& dimensions)
{
    const CutDimensionSet::ThreadType thread = dimensions.thread_type;
    const std::string& name = dimensions.name;

    std::vector<std::string>* list;
    switch (thread) {
    case CutDimensionSet::Metric:
        HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricProfile", name), dimensions);
        list = &HoleCutType_ISOmetric_Enums;
        break;
    case CutDimensionSet::MetricFine:
        HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricFineProfile", name), dimensions);
        list = &HoleCutType_ISOmetricfine_Enums;
        break;
    default:
        return;
    }
    // add the collected lists of JSON definitions to the lists
    // if a name doesn't already exist in the list
    if (std::all_of(list->begin(), list->end(),
                [name](const std::string& x) { return x != name; }))
        list->push_back(name);
}

bool Hole::isDynamicCounterbore(const std::string& thread,
    const std::string& holeCutType)
{
    CutDimensionKey key{ thread, holeCutType };
    return HoleCutTypeMap.count(key) &&
        HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Counterbore;
}

bool Hole::isDynamicCountersink(const std::string& thread,
    const std::string& holeCutType)
{
    CutDimensionKey key{ thread, holeCutType };
    return HoleCutTypeMap.count(key) &&
        HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Countersink;
}

/*
 * Counter Dimensions
 */

const Hole::CounterBoreDimension Hole::CounterBoreDimension::nothing{ "None", 0.0, 0.0 };
const Hole::CounterSinkDimension Hole::CounterSinkDimension::nothing{ "None", 0.0 };

void Hole::calculateAndSetCounterbore()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = (1.5 * threadDiameter) + 1.0;
    double k = threadDiameter;

    HoleCutDiameter.setValue(dk);
    HoleCutDepth.setValue(k);
}

void Hole::calculateAndSetCountersink()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = 2.24 * threadDiameter;
    double k = 0.62 * threadDiameter;

    HoleCutDiameter.setValue(dk);
    HoleCutDepth.setValue(k);
}


Hole::CutDimensionKey::CutDimensionKey(const std::string& t, const std::string& c) :
    thread_type{ t }, cut_name{ c }
{
}

bool Hole::CutDimensionKey::operator<(const CutDimensionKey& b) const
{
    return thread_type < b.thread_type ||
        (thread_type == b.thread_type && cut_name < b.cut_name);
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const std::string& t,
      const std::string& c) {
    return HoleCutTypeMap.find(CutDimensionKey(t, c))->second;
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const CutDimensionKey& k)
{
    return HoleCutTypeMap.find(k)->second;
}

Hole::CutDimensionSet::CutDimensionSet(const std::string& nme,
      std::vector<CounterBoreDimension>&& d, CutType cut, ThreadType thread, double a) :
    bore_data{ std::move(d) }, cut_type{ cut }, thread_type{ thread }, name{ nme }, angle{ a }
{
}

Hole::CutDimensionSet::CutDimensionSet(const std::string& nme,
      std::vector<CounterSinkDimension>&& d, CutType cut, ThreadType thread, double a) :
    sink_data{ std::move(d) }, cut_type{ cut }, thread_type{ thread }, name{ nme }, angle{ a }
{
}

const Hole::CounterBoreDimension& Hole::CutDimensionSet::get_bore(const std::string& t) const
{
    auto i = std::find_if(bore_data.begin(), bore_data.end(),
        [t](const Hole::CounterBoreDimension& x) { return x.thread == t; });
    if (i == bore_data.end())
        return CounterBoreDimension::nothing;
    else
        return *i;
}

const Hole::CounterSinkDimension& Hole::CutDimensionSet::get_sink(const std::string& t) const
{
    auto i = std::find_if(sink_data.begin(), sink_data.end(),
        [t](const Hole::CounterSinkDimension& x) { return x.thread == t; });
    if (i == sink_data.end())
        return CounterSinkDimension::nothing;
    else
        return *i;
}

void from_json(const nlohmann::json& j, Hole::CounterBoreDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
    t.depth = j["depth"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CounterSinkDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CutDimensionSet& t)
{
    t.name = j["name"].get<std::string>();

    std::string  thread_type_string = j["thread_type"].get<std::string>();
    if (thread_type_string == "metric")
        t.thread_type = Hole::CutDimensionSet::Metric;
    else if (thread_type_string == "metricfine")
        t.thread_type = Hole::CutDimensionSet::MetricFine;
    else
        THROWM(Base::IndexError, std::string("Thread type '") + thread_type_string + "' unsupported")

    std::string  cut_type_string = j["cut_type"].get<std::string>();
    if (cut_type_string == "counterbore") {
        t.cut_type = Hole::CutDimensionSet::Counterbore;
        t.bore_data = j["data"].get<std::vector<Hole::CounterBoreDimension> >();
        t.angle = 0.0;
    }
    else if (cut_type_string == "countersink") {
        t.cut_type = Hole::CutDimensionSet::Countersink;
        t.sink_data = j["data"].get<std::vector<Hole::CounterSinkDimension> >();
        t.angle = j["angle"].get<double>();
    }
    else
        THROWM(Base::IndexError, std::string("Cut type '") + cut_type_string + "' unsupported")

    t.name = j["name"].get<std::string>();
}

void Hole::readCutDefinitions()
{
    std::vector<std::string> dirs{
        ::App::Application::getResourceDir() + "Mod/PartDesign/Resources/Hole",
        ::App::Application::getUserAppDataDir() + "PartDesign/Hole"
    };

    std::clog << "Looking for thread definitions in: ";
    for (auto& i : dirs)
        std::clog << i << " ";
    std::clog << "\n";
    for (auto& dir : dirs) {
        std::vector<::Base::FileInfo> files{ ::Base::FileInfo(dir).getDirectoryContent() };
        for (const auto& f : files) {
            if (f.extension() == "json") {
                try {
                    Base::ifstream input(f);
                    nlohmann::json j;
                    input >> j;
                    CutDimensionSet screwtype = j.get<CutDimensionSet>();
                    addCutType(screwtype);
                }
                catch (std::exception& e) {
                    std::cerr << "Failed reading '" << f.filePath() << "' with: " << e.what() << "\n";
                }
            }
        }
    }
}

} // namespace PartDesign
