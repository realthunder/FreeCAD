/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

/*
 * Qt's SF Symbol icons throw on macOS 12.
 *
 * Qt 6.7 and later answer QStyle::standardIcon() and QIcon::fromTheme() on
 * Apple platforms with QAppleIconEngine, which draws an SF Symbol: ask a
 * Mac for SP_ToolBarHorizontalExtensionButton and you get an icon named
 * "chevron.forward.2". The engine's configuredImage() applies
 *
 *     [NSImageSymbolConfiguration configurationPreferringMonochrome]
 *
 * with no availability guard (qtbase, src/gui/platform/darwin/
 * qappleiconengine.mm), and that class method is macOS 13. On macOS 12 the
 * selector is unrecognised, so an Objective-C exception is thrown out of
 * every QAppleIconEngine::paint(): about thirty times in the first second
 * of a FreeCAD start, once per paint of a toolbar's overflow button and of
 * a tool button's menu arrow. GUIApplication::notify() catches each one as
 * an unknown exception and writes a crash log, the unwind leaves a painter
 * open -- which is what "QBackingStore::endPaint() called with active
 * painter" is complaining about -- and the icon never draws.
 *
 * So supply the method the OS does not have. The preference it expresses
 * is macOS 12's only behaviour: multicolour rendering is the opt-in there
 * (configurationPreferringMulticolor, macOS 12) and monochrome is what you
 * get without it, so an empty configuration says exactly the right thing.
 * Applying an empty one is a no-op by construction --
 * -configurationByApplyingConfiguration: overrides only the attributes the
 * argument sets -- and measurably so: the same symbol rendered with and
 * without the extra step gives byte-identical PNG data.
 *
 * Installed at image load, before any QIcon can exist, and only when the
 * running system lacks the method, so on macOS 13 and later this file adds
 * nothing to the process. Nothing outside it needs to know: there is no
 * header and no call site.
 *
 * The class is AppKit's but is reached by name, so linking FreeCADGui
 * against AppKit is not needed for this.
 */

#import <Foundation/Foundation.h>
#import <objc/runtime.h>

namespace
{

/*! The replacement +configurationPreferringMonochrome. Installed on the
 *  metaclass, so self is the NSImageSymbolConfiguration class object; the
 *  result is autoreleased, the convention for a +configurationXxx factory.
 */
id emptySymbolConfiguration(id self, SEL cmd)
{
    (void)cmd;
    return [[[self alloc] init] autorelease];
}

}  // namespace

@interface FreeCADMacSymbolIconCompat: NSObject
@end

@implementation FreeCADMacSymbolIconCompat

+ (void)load
{
    Class cls = NSClassFromString(@"NSImageSymbolConfiguration");
    if (!cls) {
        return;  // older than macOS 11: no SF Symbols, no QAppleIconEngine
    }

    SEL sel = NSSelectorFromString(@"configurationPreferringMonochrome");
    if ([cls respondsToSelector:sel]) {
        return;  // macOS 13 or later has it
    }

    class_addMethod(object_getClass(cls), sel, (IMP)emptySymbolConfiguration, "@@:");
}

@end
