# SPDX-License-Identifier: LGPL-2.1-or-later
"""The Qt value types the forms handle as data: colors, icons, fonts,
sizes and the enums, with Qt's own integer values so they cross to the
host unchanged.  No ipywidgets import here: PySide.QtCore and QtGui
load this eagerly and cheaply."""


class _Enum:
    """A namespace of int constants; a name outside it is an
    AttributeError naming the enum, as the linter's residue expects."""

    _name = "Qt"

    def __getattr__(self, name):
        raise AttributeError("%s.%s is not in the sandbox's Qt subset" % (self._name, name))


class _QtNamespace(_Enum):
    _name = "Qt"
    # check states
    Unchecked = 0
    PartiallyChecked = 1
    Checked = 2
    # item data roles
    DisplayRole = 0
    DecorationRole = 1
    EditRole = 2
    ToolTipRole = 3
    CheckStateRole = 10
    UserRole = 0x0100
    # item flags
    ItemIsSelectable = 1
    ItemIsEditable = 2
    ItemIsDragEnabled = 4
    ItemIsDropEnabled = 8
    ItemIsUserCheckable = 16
    ItemIsEnabled = 32
    ItemIsTristate = 64
    # orientation
    Horizontal = 1
    Vertical = 2
    # alignment
    AlignLeft = 0x0001
    AlignRight = 0x0002
    AlignHCenter = 0x0004
    AlignJustify = 0x0008
    AlignTop = 0x0020
    AlignBottom = 0x0040
    AlignVCenter = 0x0080
    AlignCenter = 0x0084
    # match flags
    MatchExactly = 0
    MatchContains = 1
    MatchStartsWith = 2
    MatchEndsWith = 3
    MatchCaseSensitive = 16
    MatchRecursive = 64
    # keys
    Key_Escape = 0x01000000
    Key_Tab = 0x01000001
    Key_Backtab = 0x01000002
    Key_Backspace = 0x01000003
    Key_Return = 0x01000004
    Key_Enter = 0x01000005
    Key_Delete = 0x01000007
    Key_Home = 0x01000010
    Key_End = 0x01000011
    Key_Left = 0x01000012
    Key_Up = 0x01000013
    Key_Right = 0x01000014
    Key_Down = 0x01000015
    Key_Space = 0x20
    # modifiers
    NoModifier = 0
    ShiftModifier = 0x02000000
    ControlModifier = 0x04000000
    AltModifier = 0x08000000
    # mouse
    LeftButton = 1
    RightButton = 2
    MiddleButton = 4
    # cursors
    ArrowCursor = 0
    WaitCursor = 3
    # text
    PlainText = 0
    RichText = 1
    TextSelectableByMouse = 1
    # misc
    CaseInsensitive = 0
    CaseSensitive = 1
    AscendingOrder = 0
    DescendingOrder = 1
    CustomContextMenu = 3
    ScrollBarAlwaysOff = 1
    ScrollBarAlwaysOn = 2
    OtherFocusReason = 7
    # colors (a QColor each, filled in below)
    transparent = None
    black = None
    white = None
    # pen and brush styles
    SolidLine = 1
    FlatCap = 0x00
    SolidPattern = 1
    OddEvenFill = 0
    # dock areas
    LeftDockWidgetArea = 1
    RightDockWidgetArea = 2
    TopDockWidgetArea = 4
    BottomDockWidgetArea = 8
    BottomRightCorner = 3


Qt = _QtNamespace()


class QColor:
    """A color as four floats; ``name()`` is ``#rrggbb`` as Qt's."""

    __slots__ = ("_rgba",)

    def __init__(self, *args):
        self._rgba = [0.0, 0.0, 0.0, 1.0]
        if len(args) == 1:
            a = args[0]
            if isinstance(a, QColor):
                self._rgba = list(a._rgba)
            elif isinstance(a, str):
                self.setNamedColor(a)
            elif isinstance(a, int):
                # 0xAARRGGBB, as QColor(QRgb) reads it
                self._rgba = [((a >> 16) & 255) / 255.0, ((a >> 8) & 255) / 255.0,
                              (a & 255) / 255.0, ((a >> 24) & 255) / 255.0]
            elif isinstance(a, (list, tuple)):
                self._set_f(*a)
        elif len(args) >= 3:
            self.setRgb(*args)

    # -- construction the corpus uses

    @classmethod
    def fromRgbF(cls, r, g, b, a=1.0):
        c = cls()
        c._set_f(r, g, b, a)
        return c

    @classmethod
    def fromRgb(cls, r, g, b, a=255):
        return cls(r, g, b, a)

    def _set_f(self, r, g, b, a=1.0):
        self._rgba = [float(r), float(g), float(b), float(a)]

    def setRgb(self, r, g, b, a=255):
        self._rgba = [r / 255.0, g / 255.0, b / 255.0, a / 255.0]

    def setRgbF(self, r, g, b, a=1.0):
        self._set_f(r, g, b, a)

    def setNamedColor(self, name):
        s = name.strip()
        if s.startswith("#"):
            h = s[1:]
            if len(h) == 6:
                self.setRgb(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
                return
            if len(h) == 8:
                # Qt reads #AARRGGBB
                self.setRgb(int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16), int(h[0:2], 16))
                return
        named = {"black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0),
                 "green": (0, 128, 0), "blue": (0, 0, 255), "yellow": (255, 255, 0),
                 "gray": (160, 160, 164), "transparent": (0, 0, 0, 0)}
        if s.lower() in named:
            self.setRgb(*named[s.lower()])
            return
        raise ValueError("QColor: unknown color name %r" % (name,))

    def setAlpha(self, a):
        self._rgba[3] = a / 255.0

    def setAlphaF(self, a):
        self._rgba[3] = float(a)

    # -- reads

    def getRgbF(self):
        return tuple(self._rgba)

    def getRgb(self):
        return tuple(int(round(c * 255)) for c in self._rgba)

    def redF(self):
        return self._rgba[0]

    def greenF(self):
        return self._rgba[1]

    def blueF(self):
        return self._rgba[2]

    def alphaF(self):
        return self._rgba[3]

    def red(self):
        return int(round(self._rgba[0] * 255))

    def green(self):
        return int(round(self._rgba[1] * 255))

    def blue(self):
        return int(round(self._rgba[2] * 255))

    def alpha(self):
        return int(round(self._rgba[3] * 255))

    def rgb(self):
        r, g, b, a = self.getRgb()
        return (0xFF << 24) | (r << 16) | (g << 8) | b

    def rgba(self):
        r, g, b, a = self.getRgb()
        return (a << 24) | (r << 16) | (g << 8) | b

    def name(self):
        return "#%02x%02x%02x" % self.getRgb()[:3]

    def isValid(self):
        return True

    def __eq__(self, other):
        return isinstance(other, QColor) and self.getRgb() == other.getRgb()

    def __hash__(self):
        return hash(self.getRgb())

    def __repr__(self):
        return "QColor(%s)" % (self.name(),)


_QtNamespace.transparent = QColor(0, 0, 0, 0)
_QtNamespace.black = QColor(0, 0, 0)
_QtNamespace.white = QColor(255, 255, 255)


class QIcon:
    """An icon by resource path (or a pixmap's); the host resolves it."""

    Normal = 0
    Disabled = 1
    Active = 2
    Selected = 3
    On = 1
    Off = 0

    __slots__ = ("path",)

    def __init__(self, source=None):
        if isinstance(source, QIcon):
            self.path = source.path
        elif isinstance(source, QPixmap):
            self.path = source.path
        elif isinstance(source, str):
            self.path = source
        elif source is None:
            self.path = ""
        else:
            raise TypeError("QIcon: a path, a QPixmap or a QIcon, not %r" % type(source).__name__)

    @staticmethod
    def fromTheme(name, fallback=None):
        icon = QIcon("theme:" + name)
        return icon

    def isNull(self):
        return not self.path

    def pixmap(self, *args):
        return QPixmap(self.path)

    def __repr__(self):
        return "QIcon(%r)" % (self.path,)


class QPixmap:
    """A pixmap by resource path; drawing into one is not in the subset."""

    __slots__ = ("path", "size")

    def __init__(self, *args):
        self.path = ""
        self.size = None
        if args and isinstance(args[0], str):
            self.path = args[0]
        elif len(args) >= 2:
            self.size = (args[0], args[1])

    def isNull(self):
        return not self.path

    def __repr__(self):
        return "QPixmap(%r)" % (self.path,)


class QFont:
    Bold = 75
    Normal = 50

    def __init__(self, family=None, pointSize=-1, weight=-1, italic=False):
        self.family = family
        self.pointSize = pointSize
        self.weight = weight
        self.italic = italic
        self.bold = weight >= QFont.Bold

    def setBold(self, on):
        self.bold = bool(on)

    def setPointSize(self, size):
        self.pointSize = size

    def setItalic(self, on):
        self.italic = bool(on)

    def setFamily(self, family):
        self.family = family


class QSize:
    __slots__ = ("w", "h")

    def __init__(self, w=-1, h=-1):
        self.w, self.h = w, h

    def width(self):
        return self.w

    def height(self):
        return self.h


class QPoint:
    __slots__ = ("_x", "_y")

    def __init__(self, x=0, y=0):
        self._x, self._y = x, y

    def x(self):
        return self._x

    def y(self):
        return self._y


class QPointF(QPoint):
    pass


class QSizePolicy:
    Fixed = 0
    Minimum = 1
    Maximum = 4
    Preferred = 5
    Expanding = 7
    MinimumExpanding = 3
    Ignored = 13

    def __init__(self, horizontal=Preferred, vertical=Preferred):
        self.horizontal = horizontal
        self.vertical = vertical


class QDialogButtonBoxButtons(_Enum):
    """QDialogButtonBox's standard buttons, Qt's values; the class
    itself lives in models (it is a widget), these are its constants."""

    _name = "QDialogButtonBox"
    NoButton = 0
    Ok = 0x00000400
    Save = 0x00000800
    SaveAll = 0x00001000
    Open = 0x00002000
    Yes = 0x00004000
    YesToAll = 0x00008000
    No = 0x00010000
    NoToAll = 0x00020000
    Abort = 0x00040000
    Retry = 0x00080000
    Ignore = 0x00100000
    Close = 0x00200000
    Cancel = 0x00400000
    Discard = 0x00800000
    Help = 0x01000000
    Apply = 0x02000000
    Reset = 0x04000000
    RestoreDefaults = 0x08000000


class QDialogButtonBox(QDialogButtonBoxButtons):
    """Until G3b makes it a widget, the enum alone (`getStandardButtons`
    returns these)."""

    def __init__(self, *args):
        raise TypeError("QDialogButtonBox as a widget is not in the sandbox yet (G3b)")


StandardButton = QDialogButtonBoxButtons
