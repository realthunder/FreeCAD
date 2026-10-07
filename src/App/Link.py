# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************
'''Utilites for generating C++ code of App::Link using Python Cog
'''
import cog, sys
from os import path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils
from params_utils import quote, auto_comment

ClassName = 'App::Link'

class PropertyInfo:
    def __init__(self, name, c_type, default=None):
        self.name = name
        self.c_type = c_type
        self.default = default if default else c_type + '{}'

PropPlacement = PropertyInfo('App::PropertyPlacement', 'Base::Placement')
PropPlacementList = PropertyInfo('App::PropertyPlacementList', 'std::vector<Base::Placement>')
PropLink = PropertyInfo('App::PropertyLink', 'App::DocumentObject*', 'nullptr')
PropLinkList = PropertyInfo('App::PropertyLinkList', 'std::vector<App::DocumentObject*>')
PropLinkSubHidden = PropertyInfo('App::PropertyLinkSubHidden', 'App::DocumentObject*', 'nullptr')
PropBool = PropertyInfo('App::PropertyBool', 'bool', 'false')
PropBoolList = PropertyInfo('App::PropertyBoolList', 'boost::dynamic_bitset<>')
PropEnum = PropertyInfo('App::PropertyEnumeration', 'long', 'long(0)')
PropFloat = PropertyInfo('App::PropertyFloat', 'double', '1.0')
PropVector = PropertyInfo('App::PropertyVector', 'Base::Vector3d', 'Base::Vector3d(1.0, 1.0 ,1.0)')
PropVectorList = PropertyInfo('App::PropertyVectorList', 'std::vector<Base::Vector3d>')
PropMatrix = PropertyInfo('App::PropertyMatrix', 'Base::Matrix4D')
PropMatrixList = PropertyInfo('App::PropertyMatrixList', 'std::vector<Base::Matrix4D>')
PropString = PropertyInfo('App::PropertyString', 'const char*', '""')
PropInteger = PropertyInfo('App::PropertyInteger', 'int', '0')
PropElementAppearance = PropertyInfo('App::PropertyElementAppearance', 'App::DocumentObject*', 'nullptr')
PropAppearanceList = PropertyInfo('App::PropertyAppearanceList', 'App::MaterialAppearance',
                                  'App::MaterialAppearance()')

class Property:
    def __init__(self, name, property_type, doc,
            default=None, derived_type=None, prop_flags=None, group=' Link'):
        self.name = name
        self.group = group
        self._type = property_type
        self.default = default if default else property_type.default
        self.doc = doc
        self.prop_flags = prop_flags if prop_flags else 'App::Prop_None'
        self.derived_type = derived_type if derived_type else self._type.name

    def declare_accessor(self):
        cog.out(f'''
    {auto_comment()}
    //@{{
    /// Accessor for property {self.name}
    {self._type.c_type} get{self.name}Value() const {{
        if (auto prop = this->props[PropIndex::Prop{self.name}])
            return static_cast<const {self._type.name} *>(prop)->getValue();
        return {self.default};
    }}
    const {self._type.name} *get{self.name}Property() const {{
        return static_cast<const {self._type.name} *>(this->props[PropIndex::Prop{self.name}]);
    }}
    {self._type.name} *get{self.name}Property() {{
        return static_cast<{self._type.name} *>(this->props[PropIndex::Prop{self.name}]);
    }}''')
        cog.out(f'''
    //@}}
''')

    def init_property(self, add_property):
        cog.out(f'''
    {add_property}({self.name}, ({self.default}), "{self.group}", {self.prop_flags}, getPropertyInfo()[PropIndex::Prop{self.name}].doc);''')


class PropertyByPointer(Property):
    '''A property that is asked for itself and not for a value: one that
    holds more than one thing, or that is a name over another's value'''

    def declare_accessor(self):
        cog.out(f'''
    {auto_comment()}
    //@{{
    /// Accessor for property {self.name}
    const {self._type.name} *get{self.name}Property() const {{
        return static_cast<const {self._type.name} *>(this->props[PropIndex::Prop{self.name}]);
    }}
    {self._type.name} *get{self.name}Property() {{
        return static_cast<{self._type.name} *>(this->props[PropIndex::Prop{self.name}]);
    }}
    //@}}
''')


class PropertyIntegerConstraint(Property):
    def __init__(self, name, doc, vmin, vmax, vstep, default=None, prop_flags=None):
        super().__init__(name, PropInteger, doc, default, 'App::PropertyIntegerConstraint', prop_flags)
        self.vmin, self.vmax, self.vstep = vmin, vmax, vstep

    def init_property(self, add_property):
        super().init_property(add_property)
        cog.out(f'''
    {{{auto_comment()}
        static const PropertyIntegerConstraint::Constraints s_constraints = {{{self.vmin}, {self.vmax}, {self.vstep}}};
        {self.name}.setConstraints(&s_constraints);
    }}''')


# A name over the looks a link holds: no value of its own, nothing in a file
LookNameFlags = '(App::PropertyType)(App::Prop_NoPersist|App::Prop_Output|App::Prop_NoRecompute)'

Properties = [
    Property('LinkPlacement', PropPlacement, "Link placement"),

    Property('Placement', PropPlacement,
        doc="Alias to LinkPlacement to make the link object compatibale with other objects"),

    Property('LinkedObject', PropLink, "Linked object", derived_type='App::PropertyXLink'),

    Property('LinkTransform', PropBool, "Set to false to override linked object's placement"),

    Property('LinkClaimChild', PropBool, "Claim the linked object as a child"),

    Property('LinkCopyOnChange', PropEnum,
        doc="Disabled: disable copy on change\n"
            "Enabled: enable copy linked object on change of any of its property marked as CopyOnChange\n"
            "Owned: force copy of the linked object (if it has not done so) and take the ownership of the copy\n"
            "Tracking: enable copy on change and auto synchronization of changes in the source object."),

    Property('LinkCopyOnChangeSource', PropLink, "The copy on change source object",
        derived_type='App::PropertyXLink'),

    Property('LinkCopyOnChangeGroup', PropLink, "Linked to a internal group object for holding on change copies"),

    Property('LinkCopyOnChangeTouched', PropBool, "Indicating the copy on change source object has been changed"),

    Property('SyncGroupVisibility', PropBool,
      "Set to false to override (nested) child visibility when linked to a plain group"),

    Property('Scale', PropFloat, "Scale factor"),

    Property('ScaleVector', PropVector,
     "Scale vector for non-uniform scaling. Please be aware that the underlying\n"
     "geometry may be transformed into BSpline surface due to non-uniform scale."),

    Property('Matrix', PropMatrix,
     "Matrix transformation for the linked object. The transformation is applied\n"
     "before scale and placement.",
     prop_flags='App::Prop_Hidden'),

    Property('PlacementList', PropPlacementList, "The placement for each element in a link array"),

    Property('AutoPlacement', PropBool, "Enable auto placement of newly created array element", default='true'),

    Property('ScaleList', PropVectorList, "The scale factors for each element in a link array", prop_flags='App::Prop_Hidden'),

    Property('MatrixList', PropMatrixList,
     "Matrix transofmration of each element in a link array.\n"
     "The transformation is applied before scale and placement.",
     prop_flags='App::Prop_Hidden'),

    Property('VisibilityList', PropBoolList, "The visibility state of element in a link arrayement"),

    PropertyIntegerConstraint('ElementCount', "Link element count", 0, 'INT_MAX', 1),

    Property('ElementList', PropLinkList, "The link element object list"),

    Property('ShowElement', PropBool, "Enable link element list", default='true'),

    Property('AutoLinkLabel', PropBool, "Enable link auto label according to linked object", prop_flags='App::Prop_Hidden'),

    Property('LinkMode', PropEnum, "Link group mode"),

    Property('LinkExecute', PropString, "Link execute function. Default to 'appLinkExecute'. 'None' to disable."),

    Property('ColoredElements', PropLinkSubHidden, "Link colored elements", prop_flags='App::Prop_Hidden'),

    # What a link lays over what it shows (docs/ShapeAppearanceDesign.md sec
    # 14.6.4): the store, and the two names over it the property editor shows
    PropertyByPointer('ElementAppearance', PropElementAppearance,
        "The looks the link gives what it shows: its own, and those of the\n"
        "elements and of the array elements it names",
        prop_flags='(App::PropertyType)(App::Prop_Hidden|App::Prop_Output)', group='Appearances'),

    Property('OverrideMaterial', PropBool, "Give what the link shows a look of the link's own",
        prop_flags=LookNameFlags, group='Appearances'),

    PropertyByPointer('ShapeAppearance', PropAppearanceList,
        "The look the link gives what it shows, where it gives one",
        prop_flags=LookNameFlags, group='Appearances'),
]

PropMap = { prop.name : prop for prop in Properties }

def declare_link_base_extension():
    cog.out(f'''
    {auto_comment()}
    /// Indices for predefined properties
    enum PropIndex {{''')
    for i,prop in enumerate(Properties):
        cog.out(f'''
        Prop{prop.name} = {i},''')
    cog.out(f'''
        PropMax
    }};
''')
    for prop in Properties:
        prop.declare_accessor()

    cog.out(f'''
    {auto_comment()}
    static const std::vector<PropInfo> &getPropertyInfo();
''')

def define_link_base_extension():
    cog.out(f'''
{auto_comment()}
const std::vector<LinkBaseExtension::PropInfo> &
LinkBaseExtension::getPropertyInfo()
{{
    static std::vector<PropInfo> PropsInfo = {{''')
    for prop in Properties:
        cog.out(f'''
        {{PropIndex::Prop{prop.name}, "{prop.name}", {prop._type.name}::getClassTypeId(),
{quote(prop.doc, indent=12)}}},''')
    cog.out(f'''
    }};
    return PropsInfo;
}}
''')


LinkExtProps = [ PropMap[name] for name in (
    'Scale',
    'ScaleVector',
    'Matrix',
    'ScaleList',
    'MatrixList',
    'VisibilityList',
    'PlacementList',
    'AutoPlacement',
    'ElementList',
)]

def declare_link_extension():
    cog.out(f'''
    {auto_comment()}''')
    for prop in LinkExtProps:
        cog.out(f'''
    {prop.derived_type} {prop.name};''')

    cog.out(f'''

    {auto_comment()}
    void registerProperties();
    void onExtendedDocumentRestored() override;
''')

def init_link_extension():
    cog.out(f'''
    {auto_comment()}''')
    for prop in LinkExtProps:
        prop.init_property('EXTENSION_ADD_PROPERTY_TYPE')
    cog.out(f'''
    registerProperties();
''')

def define_link_extension():
    cog.out(f'''
{auto_comment()}
void LinkExtension::registerProperties()
{{''')
    for prop in LinkExtProps:
        cog.out(f'''
    this->setProperty(PropIndex::Prop{prop.name}, &{prop.name});''')
    cog.out(f'''
}}
''')
    cog.out(f'''
{auto_comment()}
void LinkExtension::onExtendedDocumentRestored()
{{
    registerProperties();
    inherited::onExtendedDocumentRestored();
}}
''')

def _declare_link(props):
    cog.out(f'''
    {auto_comment()}''')
    for prop in props:
        cog.out(f'''
    {prop.derived_type} {prop.name};''')

    cog.out(f'''

    {auto_comment()}
    void registerProperties();
    void onDocumentRestored() override;
''')


def _define_link(class_name, props, view_provider=None):
    if not view_provider:
        view_provider = class_name
    cog.out(f'''
//////////////////////////////////////////////////////////////////////////////////////////
{auto_comment()}
namespace App {{
PROPERTY_SOURCE_TEMPLATE(App::{class_name}Python, App::{class_name})
template<> const char* App::{class_name}Python::getViewProviderName() const {{
    return "Gui::ViewProvider{view_provider}Python";
}}
template class AppExport FeaturePythonT<App::{class_name}>;
}} // namespace App
''')
    cog.out(f'''
//////////////////////////////////////////////////////////////////////////////////////////
{auto_comment()}
PROPERTY_SOURCE_WITH_EXTENSIONS(App::{class_name}, App::DocumentObject)
{class_name}::{class_name}()
{{''')
    for prop in props:
        prop.init_property('ADD_PROPERTY_TYPE')
    cog.out(f'''
    registerProperties();
    inherited_extension::initExtension(this);
}}
''')
    cog.out(f'''
{auto_comment()}
void {class_name}::registerProperties()
{{''')
    for prop in props:
        cog.out(f'''
    this->setProperty(PropIndex::Prop{prop.name}, &{prop.name});''')
    cog.out(f'''
}}
''')
    cog.out(f'''
{auto_comment()}
void {class_name}::onDocumentRestored()
{{
    registerProperties();
    inherited::onDocumentRestored();
}}
''')

LinkProps = [ PropMap[name] for name in (
    'LinkedObject',
    'LinkClaimChild',
    'LinkTransform',
    'LinkPlacement',
    'Placement',
    'ShowElement',
    'SyncGroupVisibility',
    'ElementCount',
    'LinkExecute',
    'ColoredElements',
    'ElementAppearance',
    'OverrideMaterial',
    'ShapeAppearance',
    'LinkCopyOnChange',
    'LinkCopyOnChangeSource',
    'LinkCopyOnChangeGroup',
    'LinkCopyOnChangeTouched',
    'AutoLinkLabel',
)]

def declare_link():
    _declare_link(LinkProps)

def define_link():
    _define_link('Link', LinkProps)

LinkElementProps = [ PropMap[name] for name in (
    'Scale',
    'ScaleVector',
    'Matrix',
    'LinkedObject',
    'LinkClaimChild',
    'LinkTransform',
    'LinkPlacement',
    'Placement',
    'LinkCopyOnChange',
    'LinkCopyOnChangeSource',
    'LinkCopyOnChangeGroup',
    'LinkCopyOnChangeTouched',
    'ElementAppearance',
    'OverrideMaterial',
    'ShapeAppearance',
)]

def declare_link_element():
    _declare_link(LinkElementProps)

def define_link_element():
    # A link element is drawn by the link's own python view provider
    _define_link('LinkElement', LinkElementProps, 'Link')

LinkGroupProps = [ PropMap[name] for name in (
    'ElementList',
    'Placement',
    'VisibilityList',
    'LinkMode',
    'ColoredElements',
    'ElementAppearance',
    'OverrideMaterial',
    'ShapeAppearance',
)]

def declare_link_group():
    _declare_link(LinkGroupProps)

def define_link_group():
    _define_link('LinkGroup', LinkGroupProps, 'Link')
