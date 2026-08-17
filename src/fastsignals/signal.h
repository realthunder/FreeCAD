/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

/** \file fastsignals/signal.h
 *  \brief Upstream's name for the signals library, spelled onto ours.
 *
 * Upstream FreeCAD vendors FastSignals (src/3rdParty/FastSignals) and its
 * core signals hand back fastsignals::connection. This fork's signals are
 * boost::signals2 and hand back boost::signals2::connection. So the two
 * names are made one namespace here rather than converted at each site:
 * ported code saying fastsignals::signal or fastsignals::scoped_connection
 * gets exactly the type our core already returns, and nothing has to be
 * rewritten in either direction.
 *
 * This is the spelling only. FastSignals itself is an API-compatible
 * drop-in for Boost.Signals2 that emits several times faster and produces
 * smaller binaries, at the cost of a few rarely used corners
 * (connect_extended, slot::track, shared_connection_block, and the
 * disconnect(slot) overload). Taking the library for those properties
 * would mean converting the fork's own signals too, which is a separate
 * decision from this header.
 */

#ifndef FC_FASTSIGNALS_SIGNAL_H
#define FC_FASTSIGNALS_SIGNAL_H

#include <boost/signals2.hpp>

namespace fastsignals = boost::signals2;

#endif  // FC_FASTSIGNALS_SIGNAL_H
