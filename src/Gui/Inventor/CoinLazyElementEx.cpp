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

#include "PreCompiled.h"

#ifndef FC_OS_WIN32
# include <dlfcn.h>
#endif

#include "CoinLazyElementEx.h"

using namespace Gui;

typedef int (*lazyex_version_t)(void);
typedef int (*lazyex_install_t)(void);
typedef int (*lazyex_get_t)(void *, const float **, uint64_t *);

static lazyex_get_t fnAmbient;
static lazyex_get_t fnEmissive;
static lazyex_get_t fnSpecular;
static lazyex_get_t fnShininess;
static bool installed;

bool CoinLazyElementEx::install()
{
    static bool tried;
    if (tried)
        return installed;
    tried = true;
#ifndef FC_OS_WIN32
    // dlsym against the already linked libCoin; every symbol must be
    // there and the surface must answer a feature level we know
    auto version = reinterpret_cast<lazyex_version_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_abi_version"));
    auto init = reinterpret_cast<lazyex_install_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_install"));
    fnAmbient = reinterpret_cast<lazyex_get_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_get_ambient"));
    fnEmissive = reinterpret_cast<lazyex_get_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_get_emissive"));
    fnSpecular = reinterpret_cast<lazyex_get_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_get_specular"));
    fnShininess = reinterpret_cast<lazyex_get_t>(
            dlsym(RTLD_DEFAULT, "coin_lazyex_get_shininess"));
    installed = version && init && fnAmbient && fnEmissive && fnSpecular
        && fnShininess && version() >= 1 && init() >= 1;
#endif
    return installed;
}

bool CoinLazyElementEx::available()
{
    return installed;
}

int CoinLazyElementEx::getAmbient(SoState *state, const float **values, uint64_t *nodeid)
{
    return installed ? fnAmbient(state, values, nodeid) : 0;
}

int CoinLazyElementEx::getEmissive(SoState *state, const float **values, uint64_t *nodeid)
{
    return installed ? fnEmissive(state, values, nodeid) : 0;
}

int CoinLazyElementEx::getSpecular(SoState *state, const float **values, uint64_t *nodeid)
{
    return installed ? fnSpecular(state, values, nodeid) : 0;
}

int CoinLazyElementEx::getShininess(SoState *state, const float **values, uint64_t *nodeid)
{
    return installed ? fnShininess(state, values, nodeid) : 0;
}
