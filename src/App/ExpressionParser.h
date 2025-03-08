/****************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>              *
 *   Copyright (c) 2019 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
 *                                                                          *
 ****************************************************************************/


#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H

#include <tuple>

#include "Expression.h"
#include <Base/Interpreter.h>
#include <Base/Matrix.h>
#include <Base/Quantity.h>
#include <Base/Vector3D.h>

namespace App {

////////////////////////////////////////////////////////////////////////////////////
// Expecting the extended expression is going to be constantly amended (to
// conform to Python), we move most of the class declarations here to avoid
// constant recompiling of the whole FC code base, as the expression header is
// included by everyone
///////////////////////////////////////////////////////////////////////////////////

_ExpressionAllocDefine(_ExpressionFastAlloc,boost::fast_pool_allocator);
#define ExpressionFastAlloc(_t) _ExpressionFastAlloc<_t> 

AppExport ExpressionPtr expressionFromPy(const App::DocumentObject *owner, const Py::Object &value);
AppExport Py::Object pyFromQuantity(const Base::Quantity &quantity);
AppExport bool pyToQuantity(Base::Quantity &q, const Py::Object &pyobj);
AppExport bool isPyMapping(const Py::Object &pyobj);

/// Check if the expression is either unit (number) expression or operator expression containing only unit
AppExport bool isSimpleExpression(const Expression *expr, bool no_arithmetics = false);

////////////////////////////////////////////////////////////////////////////////////

struct ExpressionNode;
typedef std::unique_ptr<ExpressionNode> ExpressionNodePtr;
struct ExpressionNode {
    const Expression *expr;
    int start;
    int end;
    std::vector<ExpressionNodePtr, ExpressionAllocator(ExpressionNodePtr)> children;
    std::map<App::ObjectIdentifier, bool> deps;

    void operator delete(void *p);
    static void* operator new(std::size_t sz);
};

AppExport ExpressionNodePtr getExpressionAST(std::ostringstream &ss, const Expression *expr);

/////////////////////////////////////////////////////////////////////////////////////////////

struct AppExport Expression::Component {
    ObjectIdentifier::Component comp;
    ExpressionPtr e1;
    ExpressionPtr e2;
    ExpressionPtr e3;

    explicit Component(const std::string &n);
    Component(ExpressionPtr &&e1, ExpressionPtr &&e2, ExpressionPtr &&e3, bool isRange=false);
    explicit Component(const ObjectIdentifier::Component &comp);
    Component(const Component &other);
    ~Component();
    Component &operator=(const Component &)=delete;

    static void* operator new(std::size_t sz);
    void operator delete(void *p);

    void visit(ExpressionVisitor &v);
    bool isTouched() const;
    void toString(std::ostream &ss, bool persistent) const;
    ComponentPtr copy() const;
    ComponentPtr eval() const;

    Py::Object get(const Expression *owner, const Py::Object &pyobj) const;
    void set(const Expression *owner, Py::Object &pyobj, const Py::Object &value) const;
    void del(const Expression *owner, Py::Object &pyobj) const;
};

////////////////////////////////////////////////////////////////////////////////////

struct AppExport VarInfo {
    Py::Object prefix;
    Py::Object *lhs;
    Py::Object rhs;
    Expression::ComponentPtr component;

    VarInfo()
        :lhs(0)
    {}

    VarInfo(Py::Object &v)
        :lhs(&v),rhs(v)
    {}

    VarInfo(VarInfo &&other)
        :prefix(other.prefix)
        ,lhs(other.lhs)
        ,rhs(other.rhs)
        ,component(std::move(other.component))
    {}

    VarInfo &operator=(VarInfo &&other) {
        lhs = other.lhs;
        prefix = other.prefix;
        rhs = other.rhs;
        component = std::move(other.component);
        return *this;
    }
};

/////////////////////////////////////////////////////////////////////////////////////

/// Helper class to disable calling into Python functions and writing Python attributes
class AppExport ExpressionFunctionCallDisabler
{
public:
    ExpressionFunctionCallDisabler(bool active = true);
    ~ExpressionFunctionCallDisabler();

    void setActive(bool active = true);
    bool isActive() const { return active; }
    static bool isFunctionCallDisabled();

private:
    /// Private new operator to prevent heap allocation
    void* operator new(size_t size);

private:
    bool active;

};

/// Exception thrown when invoking callable while it is disabled
class AppExport ExpressionFunctionDisabledException : Base::ExpressionError
{
public:
    using Base::ExpressionError::ExpressionError;
};

/// Helper class to block access of from expression
class AppExport ExpressionBlocker
{
public:
    ExpressionBlocker(const Expression *expr);
    ~ExpressionBlocker();
    /// Throw exception if expression is blocked
    static void check();

private:
    /// Private new operator to prevent heap allocation
    void* operator new(size_t size);
};

/////////////////////////////////////////////////////////////////////////////////////

#define EXPR_TYPESYSTEM_HEADER() \
    TYPESYSTEM_HEADER_WITH_OVERRIDE();\
public:\
    void operator delete(void *p)
        
/**
  * Part of an expressions that contains a unit.
  *
  */

class  AppExport UnitExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:

    static ExpressionPtr create(const App::DocumentObject *owner, const char *unitStr);
    static ExpressionPtr create(const App::DocumentObject *owner, const Base::Quantity &q);

    ~UnitExpression() override;

    ExpressionPtr simplify() const override;

    void setQuantity(const Base::Quantity &_quantity);

    double getValue() const { return quantity.getValue(); }

    const Base::Unit & getUnit() const { return quantity.getUnit(); }

    const Base::Quantity & getQuantity() const { return quantity; }

    const char *getUnitString() const { return unitStr; }

    double getScaler() const { return quantity.getValue(); }

protected:
    UnitExpression(const App::DocumentObject *_owner, const Base::Quantity & _quantity, const char *unit=nullptr);

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    mutable PyObject *cache = nullptr;

private:
    Base::Quantity quantity;
    const char *unitStr; /**< The unit string from the original parsed string */
};

/**
  * Class implementing a number with an optional unit
  */

class AppExport NumberExpression : public UnitExpression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *_owner, const Base::Quantity & quantity);
    static ExpressionPtr create(const App::DocumentObject *_owner, double fvalue);

    void negate();

    bool isInteger(long *v=nullptr) const;

protected:
    NumberExpression(const App::DocumentObject *_owner, const Base::Quantity &q)
        :UnitExpression(_owner,q) 
    {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
};

class AppExport ConstantExpression : public NumberExpression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner,
            const char *name, const Base::Quantity &_quantity);

    const char *getName() const { return name; }

    bool isNumber() const;
    bool isBoolean(bool *value = nullptr) const;

    ExpressionPtr simplify() const override;

protected:
    ConstantExpression(const App::DocumentObject *_owner, const Base::Quantity &q)
        :NumberExpression(_owner,q)
    {}

    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;

    const char *name; /**< Constant's name */
};

/**
  * Class implementing a reference to a property. If the name is unqualified,
  * the owner of the expression is searched. If it is qualified, the document
  * that contains the owning document object is searched for other document
  * objects to search. Both labels and internal document names are searched.
  *
  */
class AppExport VariableExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner, ObjectIdentifier &&var);

    bool isTouched() const override;

    std::string name() const;

    const ObjectIdentifier &getPath() const;

    void addComponent(ComponentPtr &&component) override;

    std::vector<std::string> getStringList() const;

    // strip out given number of component
    void popComponents(int count=1);

    VarInfo push(const Expression *owner, bool mustExist, std::string *name=0) const;

    ExpressionPtr simplify() const override;

    void assign(const ObjectIdentifier &path) const;
    void assign(Py::Object value) const;

    static const VariableExpression *isDoubleBinding(const Expression *expr);

protected:
    VariableExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void setVarInfo(VarInfo &info, bool mustExist, bool noassign=false) const;

    bool _isIndexable() const override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    void _getIdentifiers(std::map<App::ObjectIdentifier,bool> &) const override;
    bool _adjustLinks(const std::set<App::DocumentObject*> &, ExpressionVisitor &) override;
    void _importSubNames(const ObjectIdentifier::SubNameMap &) override;
    void _updateLabelReference(App::DocumentObject *, const std::string &, const char *) override;
    bool _updateElementReference(App::DocumentObject *,bool,ExpressionVisitor &) override;
    bool _relabeledDocument(const std::string &, const std::string &, ExpressionVisitor &) override;
    bool _renameObjectIdentifier(const std::map<ObjectIdentifier,ObjectIdentifier> &, 
                                         const ObjectIdentifier &, ExpressionVisitor &) override;
    void _collectReplacement(std::map<ObjectIdentifier,ObjectIdentifier> &, 
                    const App::DocumentObject *parent, App::DocumentObject *oldObj, 
                    App::DocumentObject *newObj) const override;
    void _moveCells(const CellAddress &, int, int, ExpressionVisitor &) override;
    void _offsetCells(int, int, ExpressionVisitor &) override;
    void _transposeCells(const CellAddress &,
                         const CellAddress &,
                         const CellAddress &,
                         ExpressionVisitor &) override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ObjectIdentifier var; /**< Variable name  */
};

struct ExpressionString {
    std::string text;
    char prefix0;
    char prefix1;
    char quote;

    enum QuoteType{
        QuoteNormal,
        QuoteSingle,
        QuoteDouble,
        QuoteSingleLong,
        QuoteDoubleLong,
    };

    ExpressionString(std::string &&txt = std::string())
        :text(std::move(txt))
        ,prefix0(0)
        ,prefix1(0)
        ,quote(0)
    {
    }

    ExpressionString(const ExpressionString &other)
        :text(other.text),prefix0(other.prefix0),prefix1(other.prefix1),quote(other.quote)
    {
    }
    ExpressionString(ExpressionString &&other)
        :text(std::move(other.text)),prefix0(other.prefix0),prefix1(other.prefix1),quote(other.quote)
    {
    }
    ExpressionString &operator=(const ExpressionString &other) {
        text = other.text;
        prefix0 = other.prefix0;
        prefix1 = other.prefix1;
        quote = other.quote;
        return *this;
    }
    ExpressionString &operator=(ExpressionString &&other) {
        text = std::move(other.text);
        prefix0 = other.prefix0;
        prefix1 = other.prefix1;
        quote = other.quote;
        return *this;
    }
    bool isRaw() const {
        return prefix0=='r'||prefix1=='r';
    }
    bool isFormatted() const {
        return prefix0=='f'||prefix1=='f';
    }
    bool isLong() const {
        return quote==QuoteSingleLong || quote==QuoteDoubleLong;
    }
    static ExpressionString unquote(const char *txt);
};

/**
  * Class implementing a string. Used to signal either a genuine string or
  * a failed evaluation of an expression.
  */
class AppExport StringExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    ~StringExpression();

    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionString &&text);

    static ExpressionPtr create(const App::DocumentObject *owner, const char *txt) {
        return create(owner,ExpressionString(std::string(txt?txt:"")));
    }

    const std::string &getText() const { return str.text; }

    void append(const ExpressionString &);

protected:
    explicit StringExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    bool _isIndexable() const override { return true; }
    ExpressionPtr _copy() const override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

private:
    ExpressionString str;
    mutable PyObject *cache = 0;
};

//////////////////////////////////////////////////////////////////////

class AppExport PyObjectExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, PyObject *pyobj=0);
    ~PyObjectExpression() override;

    using Expression::setPyObject; // no actual use, just silence compiler warning

    void setPyObject(Py::Object pyobj);
    void setPyObject(PyObject *pyobj, bool owned=false);

protected:
    explicit PyObjectExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &,bool, int) const override;
    ExpressionPtr _copy() const override;

protected:
    PyObject *pyObj = nullptr;
};

/**
  * Class implementing an infix expression.
  *
  */

class AppExport OperatorExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionPtr &&left, int op, ExpressionPtr &&right);

    bool isTouched() const override;

    ExpressionPtr simplify() const override;

    int priority() const override;

    int getOperator() const { return op; }

    const Expression * getLeft() const { return left.get(); }

    const Expression * getRight() const { return right.get(); }

protected:
    explicit OperatorExpression(const App::DocumentObject *_owner):Expression(_owner){}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

    bool isCommutative() const;
    bool isLeftAssociative() const;
    bool isRightAssociative() const;

    int op=0;        /**< Operator working on left and right */
    ExpressionPtr left;  /**< Left operand */
    ExpressionPtr right; /**< Right operand */
};

class AppExport AssignmentExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *_owner, 
            int catchAll, ExpressionList &&left, ExpressionList &&right, int op=0);

    static ExpressionPtr create(const App::DocumentObject *_owner, 
            ExpressionPtr &&left, ExpressionPtr &&right, int op=0);

    bool isTouched() const override;

    static Py::Object apply(const Expression *owner, int catchAll, 
            const ExpressionList &left, const Expression *right, int op=0);

protected:
    explicit AssignmentExpression(const App::DocumentObject *_owner) :Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

    static void assign(const Expression *owner, const Expression *left, PyObject *right);

protected:
    ExpressionList left;
    ExpressionPtr right;
    int catchAll = -1;
    int op = 0;
    bool rightTuple = false;
};

class AppExport ConditionalExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&condition, 
            ExpressionPtr &&trueExpr,  ExpressionPtr &&falseExpr, bool python_form=false);

    bool isTouched() const override;

    ExpressionPtr simplify() const override;

    int priority() const override;

protected:
    explicit ConditionalExpression(const App::DocumentObject *_owner)
        :Expression(_owner)
    {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

    ExpressionPtr condition;  /**< Condition */
    ExpressionPtr trueExpr;  /**< Expression if abs(condition) is > 0.5 */
    ExpressionPtr falseExpr; /**< Expression if abs(condition) is < 0.5 */
    bool pythonForm = false;
};

/**
  * Class implementing various functions, e.g sin, cos, etc.
  *
  */

class AppExport FunctionExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner, int f, ExpressionList &&args);

    bool isTouched() const override;

    ExpressionPtr simplify() const override;

    enum FunctionType {
        FUNC_NONE,

        // Normal functions taking one or two arguments
        ABS,
        ACOS,
        ASIN,
        ATAN,
        ATAN2,
        CATH,
        CBRT,
        CEIL,
        COS,
        COSH,
        EXP,
        FLOOR,
        FMOD,
        FPOW,
        HYPOT,
        LOG,
        LOG10,
        ROUND,
        SIN,
        SINH,
        SQRT,
        TAN,
        TANH,

        GET_VAR,
        HAS_VAR,
        IMPORT_PY,
        PRAGMA,

        TRUNC,

        // Matrix
        MINVERT, // invert matrix/placement/rotation
        MROTATE, // Rotate matrix/placement/rotation around axis, by rotation object, or by euler angles.
        MROTATEX, // Rotate matrix/placement/rotation around x-axis.
        MROTATEY, // Rotate matrix/placement/rotation around y-axis.
        MROTATEZ, // Rotate matrix/placement/rotation around z-axis.
        MSCALE, // matrix scale by vector
        MTRANSLATE, // Translate matrix/placement.

        // Object creation
        CREATE, // Create new object of a given type.
        LIST, // Create Python list.
        MATRIX, // Create matrix object.
        PLACEMENT, // Create placement object.
        ROTATION, // Create rotation object.
        ROTATIONX, // Create x-axis rotation object.
        ROTATIONY, // Create y-axis rotation object.
        ROTATIONZ, // Create z-axis rotation object.
        STR, // stringify
        TRANSLATIONM, // Create translation matrix object.
        TUPLE, // Create Python tuple.
        VECTOR, // Create vector object.

        HIDDENREF, // hidden reference that has no dependency check
        HIDDEN_REF = HIDDENREF,
        HREF, // deprecated alias of HIDDENREF

        // double binding, used by PropertyExpressionEngine to make a property both driving and driven
        DBIND, 

        CALLABLE_START,

        FUNC,
        FUNC_D,
        FUNC_PARSED,
        EVAL,

        CALLABLE_END,

        // Aggregates
        AGGREGATES,

        AVERAGE,
        COUNT,
        MAX,
        MIN,
        STDDEV,
        SUM,

        // Last one
        LAST,
    };
    int type() const {return ftype;}

    static Py::Object evaluate(const Expression *owner, int type, const ExpressionList &args);

    const ExpressionList &getArgs() const {return args;}

    struct FunctionInfo {
        FunctionType type;
        const char *name;
        const char *description;
    };
    static const std::vector<FunctionInfo> &getFunctions();

protected:
    explicit FunctionExpression(const App::DocumentObject *_owner):Expression(_owner){}

    static Py::Object evalAggregate(const Expression *owner, int type, const ExpressionList &args);
    static Base::Vector3d evaluateSecondVectorArgument(const Expression *expression, const ExpressionList &arguments);
    static void initialiseObject(const Py::Object *object, const ExpressionList &arguments, const unsigned long offset = 0);
    static Py::Object transformFirstArgument(
        const Expression *expression,
        const ExpressionList &arguments,
        const Base::Matrix4D *transformationMatrix);
    static Py::Object translationMatrix(double x, double y, double z);

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

    int ftype;        /**< Function to execute */
    ExpressionList args; /** Arguments to function*/
};

/**
  * Class implementing a callable expression with named arguments and optional trailing accessor
  */

class AppExport CallableExpression : public FunctionExpression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject *owner, std::string &&name,
            StringList &&names=StringList(), ExpressionList &&args=ExpressionList());

    static ExpressionPtr create(const App::DocumentObject *owner, ObjectIdentifier &&path,
            StringList &&names=StringList(), ExpressionList &&args=ExpressionList());

    static ExpressionPtr create(const DocumentObject *owner, ExpressionPtr &&expr, 
            StringList &&names=StringList(), ExpressionList &&args=ExpressionList(), 
            int ftype=0, std::string &&name = std::string(), bool checkArgs=true);

    Py::Object evaluate(PyObject *tuple, PyObject *kwds);

    VarInfo getVarInfo(bool mustExist) const;

    bool isTouched() const override;

    std::string getDocString() const;
    const std::string &getName() const {return name;}

    static void securityCheck(PyObject *pyobj, PyObject *attr);

protected:
    explicit CallableExpression(const App::DocumentObject *_owner):FunctionExpression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    bool _isIndexable() const override { return true; }
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;

    static void securityCheck(PyObject *pyobj=nullptr, const Expression *expr=nullptr);

protected:
    ExpressionPtr expr;
    std::string name;
    StringList names;
};

class AppExport RangeExpression : public App::Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
    static ExpressionPtr create(const App::DocumentObject * owner,
            std::string &&begin, std::string &&end);

    bool isTouched() const override;

    Range getRange() const;

protected:
    explicit RangeExpression(const App::DocumentObject *_owner):Expression(_owner){}

    void _toString(std::ostream &, bool, int) const override;
    ExpressionPtr _copy() const override;
    void _getIdentifiers(std::map<App::ObjectIdentifier,bool> &) const override;
    bool _renameObjectIdentifier(const std::map<ObjectIdentifier,ObjectIdentifier> &, 
                                         const ObjectIdentifier &, ExpressionVisitor &) override;
    void _moveCells(const CellAddress &, int, int, ExpressionVisitor &) override;
    void _offsetCells(int, int, ExpressionVisitor &) override;
    void _transposeCells(const CellAddress &,
                         const CellAddress &,
                         const CellAddress &,
                         ExpressionVisitor &) override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    std::string begin;
    std::string end;
};

//////////////////////////////////////////////////////////////////////

class AppExport ComprehensionExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject * owner, 
            int catchAll, ExpressionList &&targets, ExpressionPtr &&expr);

    bool isTouched() const override;

    void setExpr(ExpressionPtr &&key, ExpressionPtr &&value);
    void setExpr(ExpressionPtr &&key, bool isList=true);

    void add(int catchAll, ExpressionList &&targets, ExpressionPtr &&expr);
    void addCond(ExpressionPtr &&cond);

protected:
    explicit ComprehensionExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    bool _isIndexable() const override {return true;}
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &, bool, int) const override;
    ExpressionPtr _copy() const override;

    struct CompFor {
        ExpressionList targets;
        ExpressionPtr expr;
        ExpressionList conds;
        int catchAll;

        CompFor():catchAll(-1) 
        {}
        CompFor(CompFor &&other)
            :targets(std::move(other.targets))
            ,expr(std::move(other.expr))
            ,conds(std::move(other.conds))
            ,catchAll(other.catchAll)
        {}
        CompFor(const CompFor &other);
        CompFor &operator=(const CompFor &other) = delete;
    };
    typedef std::list<CompFor, ExpressionFastAlloc(CompFor) > CompForList;
    void _calc(Py::Object &res, CompForList::const_iterator iter) const;

protected:
    ExpressionPtr key;
    ExpressionPtr value;
    CompForList comps;
    bool list = false;
};

//////////////////////////////////////////////////////////////////////

class AppExport ListExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    typedef std::vector<bool>                       FlagList;
    typedef std::pair<ExpressionPtr, bool>          FlagExpression;
    typedef std::pair<ExpressionList, FlagList>     FlagExpressionList;

    static ExpressionPtr create(const App::DocumentObject *owner);

    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionList &&items, FlagList &&flags = FlagList());

    bool isTouched() const override;

    void printItems(std::ostream &ss, bool persistent) const;

    std::size_t getSize() const {
        return items.size();
    }

    void setItem(std::size_t index, ExpressionPtr &&expr, bool expand=false);
    void append(ExpressionPtr &&expr, bool expand=false);

    const ExpressionList &getItems() const {return items;}

protected:
    explicit ListExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    bool _isIndexable() const override {return true;}
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &, bool, int) const override;
    ExpressionPtr _copy() const override;

    friend class AssignmentExpression;

protected:
    ExpressionList items;
    FlagList flags;
};

//////////////////////////////////////////////////////////////////////

class AppExport TupleExpression : public ListExpression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner);

    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&expr, bool flag);

    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionList &&items, FlagList &&flags = FlagList());

protected:
    explicit TupleExpression(const App::DocumentObject *_owner):ListExpression(_owner) {}

    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    ExpressionPtr _copy() const override;
    void _toString(std::ostream &, bool, int) const override;
};

//////////////////////////////////////////////////////////////////////

class AppExport DictExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject * owner);

    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionPtr &&key, ExpressionPtr &&value);

    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&value);

    bool isTouched() const override;

    void addItem(ExpressionPtr &&key, ExpressionPtr &&value);
    void addItem(ExpressionPtr &&value);

protected:
    explicit DictExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    bool _isIndexable() const override {return true;}
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &, bool, int) const override;
    ExpressionPtr _copy() const override;

protected:
    ExpressionList keys;
    ExpressionList values;
};

//////////////////////////////////////////////////////////////////////

class AppExport IDictExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject * owner);

    static ExpressionPtr create(const App::DocumentObject * owner, 
            std::string &&key, ExpressionPtr &&value);

    static ExpressionPtr create(const App::DocumentObject * owner, 
            const char *key, ExpressionPtr &&value);

    bool isTouched() const override;

    void addItem(std::string &&key, ExpressionPtr &&value);
    void addItem(const char *key, ExpressionPtr &&value);

    std::map<std::string, ExpressionPtr> getItems() const;
    
protected:
    explicit IDictExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    bool _isIndexable() const override {return true;}
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;
    void _toString(std::ostream &, bool, int) const override;
    ExpressionPtr _copy() const override;

protected:
    StringList keys;
    ExpressionList values;
};

/////////////////////////////////////////////////////////////////

class AppExport BaseStatement : public Expression {
    EXPR_TYPESYSTEM_HEADER();
public:
protected:
    explicit BaseStatement(const App::DocumentObject *owner):Expression(owner){}
};

/////////////////////////////////////////////////////////////////

class AppExport PseudoStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, int type);

protected:
    PseudoStatement(const App::DocumentObject *_owner,int _t)
        :BaseStatement(_owner),type(_t)
    {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    int type;
};

/////////////////////////////////////////////////////////////////

class AppExport JumpStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, 
                    int type, ExpressionPtr &&expr = ExpressionPtr());

    bool isTouched() const override;

protected:
    explicit JumpStatement(const App::DocumentObject *_owner) :BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionPtr expr;
    int type = 0;
};

/////////////////////////////////////////////////////////////////

class AppExport IfStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionPtr &&condition, ExpressionPtr &&statement);

    void add(ExpressionPtr &&condition, ExpressionPtr &&statement);
    void addElse(ExpressionPtr &&statement);

    bool isTouched() const override;
    bool needLineEnd() const override;

protected:
    explicit IfStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionList conditions;
    ExpressionList statements;
};

/////////////////////////////////////////////////////////////////

class AppExport WhileStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, 
            ExpressionPtr &&condition, ExpressionPtr &&statement);

    void addElse(ExpressionPtr &&expr);

    bool isTouched() const override;
    bool needLineEnd() const override;

protected:
    explicit WhileStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionPtr condition;
    ExpressionPtr statement;
    ExpressionPtr else_expr;
};

/////////////////////////////////////////////////////////////////

class AppExport ForStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, 
            int catchAll, ExpressionList &&targets, ExpressionList &&exprs, ExpressionPtr &&statement);

    void addElse(ExpressionPtr &&expr);

    bool isTouched() const override;
    bool needLineEnd() const override;

protected:
    explicit ForStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionList targets;
    ExpressionPtr value;
    ExpressionPtr statement;
    ExpressionPtr else_expr;
    int catchAll = -1;
    bool valueTuple = false;
};

/////////////////////////////////////////////////////////////////

class AppExport SimpleStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&expr);

    bool isTouched() const override;
    void add(ExpressionPtr &&expr);

    std::size_t getSize() const {return exprs.size();}
    const Expression *getExpr(std::size_t idx) const;

    template<typename T>
    static const T *cast(const Expression *expr) {
        auto res = Base::freecad_dynamic_cast<const T>(expr);
        if(res)
            return res;
        auto stmt = Base::freecad_dynamic_cast<const SimpleStatement>(expr);
        if(stmt && stmt->getSize()==1)
            return cast<T>(stmt->getExpr(0));
        return 0;
    }

    ExpressionPtr reduce() const;

protected:
    explicit SimpleStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionList exprs;
};

/////////////////////////////////////////////////////////////

class AppExport Statement : public SimpleStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&expr);

protected:
    explicit Statement(const App::DocumentObject *_owner):SimpleStatement(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
};

/////////////////////////////////////////////////////////////

class AppExport LambdaExpression : public Expression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&body, 
            StringList &&names=StringList(), ExpressionList &&args=ExpressionList());

    bool isTouched() const override;

protected:
    explicit LambdaExpression(const App::DocumentObject *_owner):Expression(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    StringList names;
    ExpressionList args;
    ExpressionPtr body;
};

/////////////////////////////////////////////////////////////

class AppExport FunctionStatement : public LambdaExpression {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, std::string &&name, 
            ExpressionPtr &&body, StringList &&names=StringList(), ExpressionList &&args=ExpressionList());

    bool needLineEnd() const override;

    const std::string &getName() const {return name;}

protected:
    explicit FunctionStatement(const App::DocumentObject *_owner):LambdaExpression(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    std::string name;
};


/////////////////////////////////////////////////////////////

class AppExport DelStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionList &&targets);

    bool isTouched() const override;

protected:
    explicit DelStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _visit(ExpressionVisitor & v) override;
    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    ExpressionList targets;
};

/////////////////////////////////////////////////////////////

class AppExport ScopeStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, StringList &&names, bool global=true); 

protected:
    explicit ScopeStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    StringList names;
    bool global = true;
};

/////////////////////////////////////////////////////////////

class AppExport TryStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, ExpressionPtr &&body);

    void add(ExpressionPtr &&body, ExpressionPtr &&expr=ExpressionPtr(), std::string &&name=std::string());
    void addElse(ExpressionPtr &&body);
    void addFinal(ExpressionPtr &&body);
    void check();
    bool needLineEnd() const override;

protected:
    explicit TryStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

    bool findException(Py::Object &res, int *jumpCode, Base::Exception &e, PyObject *pyobj) const;

protected:
    ExpressionPtr body;
    StringList names;
    ExpressionList exprs;
    ExpressionList bodies;
    ExpressionPtr else_body;
    ExpressionPtr final_body;
};

/////////////////////////////////////////////////////////////

class AppExport ImportStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, std::string &&module,
            std::string &&name = std::string());

    void add(std::string &&module, std::string &&name = std::string());

protected:
    explicit ImportStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    StringList modules;
    StringList names;
};

/////////////////////////////////////////////////////////////////////////////////////

class AppExport FromStatement : public BaseStatement {
    EXPR_TYPESYSTEM_HEADER();

public:
    static ExpressionPtr create(const App::DocumentObject *owner, std::string &&module,
            std::string &&tail, std::string &&name = std::string());

    void add(std::string &&tail, std::string &&name = std::string());

protected:
    explicit FromStatement(const App::DocumentObject *_owner):BaseStatement(_owner) {}

    void _toString(std::ostream &ss, bool persistent, int indent) const override;
    ExpressionPtr _copy() const override;
    Py::Object _getPyValue(int *jumpCode=nullptr) const override;

protected:
    std::string module;
    StringList tails;
    StringList names;
};

/////////////////////////////////////////////////////////////////////////////////////

namespace ExpressionParser {

AppExport ExpressionPtr parse(const App::DocumentObject *owner, 
        const char *buffer, std::size_t len=0, bool verbose=false, bool pythonMode=false);
AppExport ExpressionPtr parseUnit(const App::DocumentObject *owner, const char *buffer);
AppExport ObjectIdentifier parsePath(const App::DocumentObject *owner, const char* buffer, std::size_t len=0);
AppExport bool isTokenAnIndentifier(const std::string & str);
AppExport bool isTokenAUnit(const std::string & str);

AppExport std::vector<std::tuple<int, int, std::string> > tokenize(const char *str);

inline std::vector<std::tuple<int, int, std::string> > tokenize(const std::string & str) {
    return tokenize(str.c_str());
}

AppExport void clearWarning();

enum TokenType {
    FC_TOK_END,
    FC_TOK_LITERAL,
    FC_TOK_KEYWORD,
    FC_TOK_IDENTIFIER,
    FC_TOK_STRING,
    FC_TOK_UNIT,
    FC_TOK_CELLADDRESS,
    FC_TOK_NUMBER,
    FC_TOK_OPERATOR,
    FC_TOK_OTHER,
};
AppExport int translateToken(int t);

/// Convenient class to mark begin of importing
class AppExport ExpressionImporter {
public:
    explicit ExpressionImporter(Base::XMLReader &reader);
    ~ExpressionImporter();
    static Base::XMLReader *reader();
};

AppExport bool isModuleImported(PyObject *);

} // end of namespace ExpressionParser
} // end of namespace App

#endif //EXPRESSION_PARSER_H

