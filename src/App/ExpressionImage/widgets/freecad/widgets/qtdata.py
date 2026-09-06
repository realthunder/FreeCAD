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
    StatusTipRole = 4
    WhatsThisRole = 5
    FontRole = 6
    TextAlignmentRole = 7
    BackgroundRole = 8
    ForegroundRole = 9
    CheckStateRole = 10
    AccessibleTextRole = 11
    AccessibleDescriptionRole = 12
    SizeHintRole = 13
    InitialSortOrderRole = 14
    UserRole = 0x0100
    # item flags
    ItemIsSelectable = 1
    ItemIsEditable = 2
    ItemIsDragEnabled = 4
    ItemIsDropEnabled = 8
    ItemIsUserCheckable = 16
    ItemIsEnabled = 32
    ItemIsTristate = 64
    ItemIsAutoTristate = 64
    ItemNeverHasChildren = 128
    ItemIsUserTristate = 256
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
    MatchRegularExpression = 4
    MatchWildcard = 5
    MatchFixedString = 8
    MatchWrap = 32
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
    """A pixmap by resource path, or one made of an image drawn in code
    (`fromImage`: the drawing as an SVG data URI the host renders)."""

    __slots__ = ("path", "size")

    def __init__(self, *args):
        self.path = ""
        self.size = None
        if args and isinstance(args[0], str):
            self.path = args[0]
        elif len(args) >= 2:
            self.size = (args[0], args[1])

    @staticmethod
    def fromImage(image, *args):
        pm = QPixmap()
        pm.path = image._data_uri()
        pm.size = (image.width(), image.height())
        return pm

    def isNull(self):
        return not self.path

    def width(self):
        return self.size[0] if self.size else 0

    def height(self):
        return self.size[1] if self.size else 0

    def scaled(self, *args, **kwargs):
        return self

    def toImage(self):
        im = QImage(self.width(), self.height())
        im._path = self.path
        return im

    def __repr__(self):
        return "QPixmap(%r)" % (self.path,)


class QFont:
    """A font as data: what `setFont` carries to the host (bold, italic,
    pointSize, family), the widget's own font for the rest."""

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

    def setPointSizeF(self, size):
        self.pointSize = int(size)

    def setItalic(self, on):
        self.italic = bool(on)

    def setFamily(self, family):
        self.family = family

    def setWeight(self, weight):
        self.weight = weight
        self.bold = weight >= QFont.Bold

    def setUnderline(self, on):
        self.underline = bool(on)

    def family_(self):
        return self.family

    def toDict(self):
        d = {"bold": self.bold, "italic": self.italic}
        if self.pointSize and self.pointSize > 0:
            d["pointSize"] = int(self.pointSize)
        if self.family:
            d["family"] = str(self.family)
        return d

    @staticmethod
    def fromDict(d):
        f = QFont(d.get("family"), d.get("pointSize", -1), -1, bool(d.get("italic", False)))
        f.bold = bool(d.get("bold", False))
        return f


class QFontMetrics:
    """An estimate: nothing is measured in the guest.  About seven
    pixels a character, sixteen high (the host lays the real text out)."""

    CHAR_WIDTH = 7
    LINE_HEIGHT = 16

    def __init__(self, font=None):
        self.font = font

    def width(self, text):
        return self.CHAR_WIDTH * len(str(text))

    def horizontalAdvance(self, text):
        return self.width(text)

    def height(self):
        return self.LINE_HEIGHT

    def boundingRect(self, text):
        return QRect(0, 0, self.width(text), self.LINE_HEIGHT)


QFontMetricsF = QFontMetrics


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


StandardButton = QDialogButtonBoxButtons


class QMargins:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self._m = [int(left), int(top), int(right), int(bottom)]

    def left(self):
        return self._m[0]

    def top(self):
        return self._m[1]

    def right(self):
        return self._m[2]

    def bottom(self):
        return self._m[3]


# -- events (G3c): what the host relays to a widget that asked --------------


class QEvent:
    """A relayed QEvent: Qt's own type values, so `event.type() ==
    QtCore.QEvent.KeyPress` reads as natively."""

    MouseButtonPress = 2
    MouseButtonRelease = 3
    MouseButtonDblClick = 4
    MouseMove = 5
    KeyPress = 6
    KeyRelease = 7
    FocusIn = 8
    FocusOut = 9
    Enter = 10
    Leave = 11
    Paint = 12
    Move = 13
    Resize = 14
    Show = 17
    Hide = 18
    Close = 19
    Wheel = 31
    LanguageChange = 89
    ContextMenu = 82

    Type = None  # filled below: the enum namespace

    def __init__(self, type_):
        self._type = int(type_)
        self._accepted = True
        self._to_host = True

    def type(self):
        return self._type

    def accept(self):
        self._accepted = True

    def ignore(self):
        self._accepted = False

    def isAccepted(self):
        return self._accepted

    def setAccepted(self, on):
        self._accepted = bool(on)

    def spontaneous(self):
        return True

    @staticmethod
    def make(args):
        """The event the host's relay described: `[type, ...]`."""
        t = int(args[0])
        if t in (QEvent.KeyPress, QEvent.KeyRelease):
            return QKeyEvent(t, *args[1:])
        if t in (QEvent.MouseButtonPress, QEvent.MouseButtonRelease,
                 QEvent.MouseButtonDblClick, QEvent.MouseMove):
            return QMouseEvent(t, *args[1:])
        if t in (QEvent.FocusIn, QEvent.FocusOut):
            return QFocusEvent(t, *args[1:])
        return QEvent(t)


QEvent.Type = QEvent


class QKeyEvent(QEvent):
    def __init__(self, type_, key=0, modifiers=0, text="", autorep=False, count=1):
        QEvent.__init__(self, type_)
        self._key = int(key)
        self._modifiers = int(modifiers)
        self._text = str(text or "")
        self._autorep = bool(autorep)
        self._count = int(count)

    def key(self):
        return self._key

    def modifiers(self):
        return self._modifiers

    def text(self):
        return self._text

    def isAutoRepeat(self):
        return self._autorep

    def count(self):
        return self._count

    def matches(self, key):
        return False


class QMouseEvent(QEvent):
    def __init__(self, type_, x=0, y=0, button=0, buttons=0, modifiers=0):
        QEvent.__init__(self, type_)
        self._pos = QPoint(int(x), int(y))
        self._button = int(button)
        self._buttons = int(buttons)
        self._modifiers = int(modifiers)

    def pos(self):
        return self._pos

    def position(self):
        return QPointF(self._pos.x(), self._pos.y())

    def x(self):
        return self._pos.x()

    def y(self):
        return self._pos.y()

    def button(self):
        return self._button

    def buttons(self):
        return self._buttons

    def modifiers(self):
        return self._modifiers


class QFocusEvent(QEvent):
    def __init__(self, type_, reason=7):
        QEvent.__init__(self, type_)
        self._reason = int(reason)

    def reason(self):
        return self._reason

    def gotFocus(self):
        return self._type == QEvent.FocusIn

    def lostFocus(self):
        return self._type == QEvent.FocusOut


class QCursor:
    """The cursor as the guest sees it: a position the host reports
    when asked (`getMainWindow().cursor().pos()`), a shape as data."""

    def __init__(self, shape=0):
        self.shape = shape

    @staticmethod
    def pos():
        import _fcx

        p = _fcx.op("gui.mainwindow", 0, ["cursorPos"])
        return QPoint(int(p[0]), int(p[1]))

    def setPos(self, *args):
        pass


class QKeySequence:
    def __init__(self, keys=""):
        self._keys = str(keys)

    def toString(self, *args):
        return self._keys

    def __str__(self):
        return self._keys


# -- painting in code (G3c): a QImage drawn with a QPainter crosses as
# an SVG the host renders (DraftGui's style button); nothing is
# rasterized here.


class QPen:
    def __init__(self, color=None, width=1, style=1, cap=0, join=0):
        self.color = color if isinstance(color, QColor) else QColor(color) if color else None
        self.width = width
        self.style = style
        self.cap = cap
        self.join = join

    def setColor(self, color):
        self.color = QColor(color)

    def setWidth(self, width):
        self.width = width

    def setStyle(self, style):
        self.style = style


class QBrush:
    def __init__(self, color=None, style=1):
        if isinstance(color, int) and not isinstance(color, bool) and not isinstance(color, QColor):
            # QBrush(Qt.NoBrush)
            self.color, self.style = None, color
        else:
            self.color = color if isinstance(color, QColor) else QColor(color) if color else None
            self.style = style

    def setColor(self, color):
        self.color = QColor(color)

    def setStyle(self, style):
        self.style = style


class QImage:
    Format_ARGB32 = 5
    Format_RGB32 = 4
    Format_ARGB32_Premultiplied = 6

    def __init__(self, *args):
        self._w, self._h = 0, 0
        self._ops = []
        self._fill = None
        self._path = ""
        if len(args) >= 2 and isinstance(args[0], int):
            self._w, self._h = int(args[0]), int(args[1])
        elif args and isinstance(args[0], str):
            self._path = args[0]
        elif args and isinstance(args[0], QSize):
            self._w, self._h = args[0].width(), args[0].height()

    def width(self):
        return self._w

    def height(self):
        return self._h

    def size(self):
        return QSize(self._w, self._h)

    def isNull(self):
        return not (self._w and self._h) and not self._path

    def fill(self, color):
        c = color if isinstance(color, QColor) else QColor(color)
        self._fill = None if c.alpha() == 0 else c

    def save(self, *args):
        return False

    def scaled(self, *args, **kwargs):
        return self

    def _svg(self):
        body = []
        if self._fill is not None:
            body.append('<rect width="%d" height="%d" fill="%s"/>'
                        % (self._w, self._h, self._fill.name()))
        body.extend(self._ops)
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
                'viewBox="0 0 %d %d">%s</svg>' % (self._w, self._h, self._w, self._h,
                                                  "".join(body)))

    def _data_uri(self):
        if self._path:
            return self._path
        return "data:image/svg+xml;utf8," + self._svg()


def _style_attrs(pen, brush):
    attrs = []
    if brush is not None and brush.color is not None and brush.style:
        attrs.append('fill="%s"' % brush.color.name())
        if brush.color.alpha() < 255:
            attrs.append('fill-opacity="%.3f"' % (brush.color.alpha() / 255.0))
    else:
        attrs.append('fill="none"')
    if pen is not None and pen.color is not None and pen.style:
        attrs.append('stroke="%s"' % pen.color.name())
        attrs.append('stroke-width="%s"' % pen.width)
        if pen.color.alpha() < 255:
            attrs.append('stroke-opacity="%.3f"' % (pen.color.alpha() / 255.0))
    return " ".join(attrs)


class QPainter:
    Antialiasing = 1

    def __init__(self, device=None):
        self._device = device
        self._pen = QPen(Qt.black)
        self._brush = QBrush(None, 0)
        self._active = device is not None

    def begin(self, device):
        self._device = device
        self._active = True
        return True

    def end(self):
        self._active = False
        return True

    def isActive(self):
        return self._active

    def setPen(self, pen):
        self._pen = pen if isinstance(pen, QPen) else QPen(pen)

    def setBrush(self, brush):
        self._brush = brush if isinstance(brush, QBrush) else QBrush(brush)

    def setRenderHint(self, *args):
        pass

    def setFont(self, font):
        pass

    def _emit(self, element):
        if isinstance(self._device, QImage):
            self._device._ops.append(element)

    def drawPolygon(self, points, *args):
        pts = " ".join("%g,%g" % (p.x(), p.y()) for p in points)
        self._emit('<polygon points="%s" %s/>' % (pts, _style_attrs(self._pen, self._brush)))

    def drawRect(self, *args):
        if len(args) == 1:
            r = args[0]
            x, y, w, h = r.x(), r.y(), r.width(), r.height()
        else:
            x, y, w, h = args[:4]
        self._emit('<rect x="%g" y="%g" width="%g" height="%g" %s/>'
                   % (x, y, w, h, _style_attrs(self._pen, self._brush)))

    def fillRect(self, *args):
        brush = args[-1]
        if not isinstance(brush, QBrush):
            brush = QBrush(brush)
        pen, self._pen = self._pen, QPen(None, 0, 0)
        try:
            self.drawRect(*args[:-1])
        finally:
            self._pen = pen

    def drawEllipse(self, *args):
        if len(args) == 1:
            r = args[0]
            x, y, w, h = r.x(), r.y(), r.width(), r.height()
        else:
            x, y, w, h = args[:4]
        self._emit('<ellipse cx="%g" cy="%g" rx="%g" ry="%g" %s/>'
                   % (x + w / 2.0, y + h / 2.0, w / 2.0, h / 2.0,
                      _style_attrs(self._pen, self._brush)))

    def drawLine(self, *args):
        if len(args) == 2:
            x1, y1, x2, y2 = args[0].x(), args[0].y(), args[1].x(), args[1].y()
        else:
            x1, y1, x2, y2 = args[:4]
        self._emit('<line x1="%g" y1="%g" x2="%g" y2="%g" %s/>'
                   % (x1, y1, x2, y2, _style_attrs(self._pen, None)))

    def drawText(self, *args):
        pass


class QRect:
    def __init__(self, x=0, y=0, w=0, h=0):
        self._x, self._y, self._w, self._h = int(x), int(y), int(w), int(h)

    def x(self):
        return self._x

    def y(self):
        return self._y

    def width(self):
        return self._w

    def height(self):
        return self._h

    def center(self):
        return QPoint(self._x + self._w // 2, self._y + self._h // 2)

    def topLeft(self):
        return QPoint(self._x, self._y)

    def size(self):
        return QSize(self._w, self._h)

    def isValid(self):
        return self._w > 0 and self._h > 0


def __getattr__(name):
    # `PySide.QtWidgets` imports QDialogButtonBox from here by name (the
    # shim predates G3b); the widget class lives with the models
    if name == "QDialogButtonBox":
        from . import models

        return models.QDialogButtonBox
    raise AttributeError("module %r has no attribute %r" % (__name__, name))
