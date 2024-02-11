/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef GUI_PREFWIDGETS_H
#define GUI_PREFWIDGETS_H

#include <boost_signals2.hpp>

#include <memory>
#include <QVector>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFont>
#include <QTimer>
#include <QVariant>
#include <QRadioButton>
#include <QTextEdit>

#include <Base/Bitmask.h>
#include <Base/Parameter.h>

#include "Widgets.h"
#include "FileDialog.h"
#include "SpinBox.h"
#include "QuantitySpinBox.h"
#include "Window.h"


class QSplitter;

namespace Gui {
class CommandManager;
class WidgetFactoryInst;

/** The preference widget class.
 * If you want to extend a QWidget class to save/restore its data
 * you just have to derive from this class and implement the methods
 * restorePreferences() and savePreferences().
 *
 * To restore and save the settings of any widgets in own dialogs you have
 * call onRestore() e.g. in the dialog's constructor and call onSave() e.g.
 * in accept() for each widget you want to enable this mechanism.
 *
 * For more information of how to use these widgets in normal container widgets
 * which are again in a dialog refer to the description of Gui::Dialog::DlgPreferencesImp.
 *
 * \author Werner Mayer
 */
class GuiExport PrefWidget : public WindowParameter
{
public:
  virtual void setEntryName( const QByteArray& name );
  QByteArray entryName() const;
  /** Does the same as setEntryName().
   * This function is added for convenience because the ui compiler
   * will use this function if the attribute stdset isn't set to 0 in a .ui file.
   */
  void setPrefEntry(const QByteArray& name);

  virtual void setParamGrpPath( const QByteArray& path );
  QByteArray paramGrpPath() const;
  /** Does the same as setParamGrpPath().
   * This function is added for convenience because the ui compiler
   * will use this function if the attribute stdset isn't set to 0 in a .ui file.
   */
  void setPrefPath(const QByteArray& name);

  void OnChange(Base::Subject<const char*> &rCaller, const char * sReason) override;
  void onSave();
  void onRestore();

  enum class EntryType {
      Bool,
      Int,
      Double,
      String,
  };
  struct SubEntry {
      QByteArray name;
      QString displayName;
      EntryType type;
      QVariant defvalue;
  };
  void setSubEntries(QObject *base, const QVector<SubEntry> &entires);

  enum class EntryFlag {
    Decimals = 1,
    Minimum = 2,
    Maximum = 4,
    Step = 8,
    Int = 16,
    All = Decimals|Minimum|Maximum|Step,
    Default = Decimals|Step,
  };
  using EntryFlags = Base::Flags<EntryFlag>;
  void setupSubEntries(QObject *base, EntryFlags flags = EntryFlag::Default);

  typedef std::function<bool(const QByteArray &, QVariant &)>  SubEntryValidate;
  void setSubEntryValidate(const SubEntryValidate &);

  const QVector<SubEntry> & subEntries() const;

  static QVariant getSubEntryValue(const QByteArray &entryName,
                                   const QByteArray &name,
                                   EntryType type,
                                   const QVariant &defvalue);
  static void resetSubEntries();

  void restoreSubEntries();
  void saveSubEntries();

  boost::signals2::signal<void (QObject *, const SubEntry *)> signalSubEntryChanged;

  virtual void setAutoSave(bool enable) = 0;

  /** Initial auto save
   *
   * @param enable: enable auto save
   *
   * This is a convenience function that calls onRestore(), and then
   * setAutoSave(enable), and finally PrefParam::removeEntry() to detach auto
   * save setting from PrefParam::AutoSave() parameter.
   */
  void initAutoSave(bool enable = true);

  template<class SignalT, class O>
  void autoSave(bool enable, O *o, SignalT signal, int delay=100) {
    if (m_Conn) {
      QObject::disconnect(m_Conn);
      if (!enable)
          return;
    }
    if (delay) {
      if (!m_Timer) {
          m_Timer.reset(new QTimer);
          m_Timer->setSingleShot(true);
          QObject::connect(m_Timer.get(), &QTimer::timeout, [this](){onSave();});
      }
      m_Conn = QObject::connect(o, signal, [this, delay](){
        if (!m_Busy && m_Restored)
            m_Timer->start(delay);
      });
    } else {
      m_Conn = QObject::connect(o, signal, [this](){
        if (!m_Busy && m_Restored)
            onSave();
      });
    }
  }

protected:
  /** Restores the preferences
   * Must be reimplemented in any subclasses.
   */
  virtual void restorePreferences() = 0;
  /** Save the preferences
   * Must be reimplemented in any subclasses.
   */
  virtual void savePreferences()    = 0;
  /** Print warning that saving failed.
   */
  void failedToSave(const QString&) const;
  /** Print warning that restoring failed.
   */
  void failedToRestore(const QString&) const;

  void buildContextMenu(QMenu *menu);

  bool restoreSubEntry(const SubEntry &, const char *change = nullptr);

  QByteArray entryPrefix();
  ParameterGrp::handle getEntryParameter();

  PrefWidget();
  ~PrefWidget() override;

private:
  QObject *m_Base = nullptr;
  QByteArray m_sPrefName;
  QByteArray m_sPrefGrp;
  QVector<SubEntry> m_SubEntries;
  SubEntryValidate m_Validate;
  ParameterGrp::handle m_EntryHandle;

  // friends
  friend class Gui::WidgetFactoryInst;

  QMetaObject::Connection m_Conn;
  std::unique_ptr<QTimer> m_Timer;
  bool m_Busy = false;

protected:
  bool m_Restored = false;
};


/// Convenient class for accessing and tracking common parameter settings of PrefWidgets
class PrefParam: public ParameterGrp::ObserverType {
public:
  PrefParam();
  ~PrefParam();

  void OnChange(Base::Subject<const char*> &, const char * sReason);
  static void addEntry(PrefWidget *entry);
  static void removeEntry(PrefWidget *entry);
  static bool AutoSave();
  static void setAutoSave(bool);
  static PrefParam *instance();

private:
  ParameterGrp::handle hGrp;
  bool _AutoSave;
  std::set<PrefWidget*> _entries;
};


/// Convenient class to help widget save/restore states
class GuiExport PrefWidgetStates : public QObject
{
  Q_OBJECT
public:
  explicit PrefWidgetStates(QWidget *widget,
                            bool manageSize=true,
                            const char *name = nullptr,
                            QObject *parent = nullptr);
  virtual ~PrefWidgetStates();
  void addSplitter(QSplitter *, const char *name = nullptr);

protected:
  bool eventFilter(QObject *o, QEvent *e);
  void saveSettings();
  void restoreSettings();
  
protected:
  ParameterGrp::handle hParam;
  bool geometryRestored = false;
  bool manageSize = true;
  std::map<QSplitter*, std::string> splitters;
  QWidget *widget = nullptr;
};

/** The PrefSpinBox class.
 * \author Werner Mayer
 */
class GuiExport PrefSpinBox : public IntSpinBox, public PrefWidget
{
  typedef IntSpinBox inherited;

  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefSpinBox ( QWidget * parent = nullptr );
  ~PrefSpinBox() override;

  void setEntryName( const QByteArray& name ) override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;
  void contextMenuEvent(QContextMenuEvent *event) override;

private:
  int m_Default;
};

/** The PrefDoubleSpinBox class.
 * \author Werner Mayer
 */
class GuiExport PrefDoubleSpinBox : public DoubleSpinBox, public PrefWidget
{
  typedef DoubleSpinBox inherited;

  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefDoubleSpinBox ( QWidget * parent = nullptr );
  ~PrefDoubleSpinBox() override;

  void setEntryName( const QByteArray& name ) override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;
  void contextMenuEvent(QContextMenuEvent *event) override;

private:
  double m_Default;
};

/**
 * The PrefLineEdit class.
 * \author Werner Mayer
 */
class GuiExport PrefLineEdit : public QLineEdit, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefLineEdit ( QWidget * parent = nullptr );
  ~PrefLineEdit() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  QString m_Default;
};

/**
 * The PrefAccelLineEdit class.
 */
class GuiExport PrefAccelLineEdit : public AccelLineEdit, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     )
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  )

public:
  explicit PrefAccelLineEdit ( QWidget * parent = nullptr );
  ~PrefAccelLineEdit() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  QString m_Default;
};

/**
 * The PrefLineEdit class.
 * \author Chris Hennes
 */
class GuiExport PrefTextEdit : public QTextEdit, public PrefWidget
{
Q_OBJECT

    Q_PROPERTY(QByteArray prefEntry READ entryName     WRITE setEntryName) // clazy:exclude=qproperty-without-notify
    Q_PROPERTY(QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath) // clazy:exclude=qproperty-without-notify

public:
    explicit PrefTextEdit(QWidget* parent = nullptr);
    ~PrefTextEdit() override;
    void setAutoSave(bool enable) override;

Q_SIGNALS:
    void focusChanged();

protected:
    // restore from/save to parameters
    void restorePreferences() override;
    void savePreferences() override;
    void focusOutEvent(QFocusEvent *) override;

private:
    QString m_Default;
};

/**
 * The PrefFileChooser class.
 * \author Werner Mayer
 */
class GuiExport PrefFileChooser : public FileChooser, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefFileChooser ( QWidget * parent = nullptr );
  ~PrefFileChooser() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  QString m_Default;
};

/**
 * The PrefComboBox class.
 * \author Werner Mayer
 *
 * The PrefComboBox supports restoring/saving variant type of item data. You
 * can add a property named 'prefType' with the type you want. If no such
 * property is found, the class defaults to restore/save the item index.
 *
 * Note that there is special handling for 'prefType' of QString, which means
 * to restore/save the item text. This allows the combox to be editable, and
 * accepts user entered value. Use QByteArray if you want to restore/save a
 * non translatable string stored as item data.
 */
class GuiExport PrefComboBox : public QComboBox, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefComboBox ( QWidget * parent = nullptr );
  ~PrefComboBox() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;
  virtual QMetaType::Type getParamType() const;

private:
  QVariant m_Default;
  int m_DefaultIndex;
  QString m_DefaultText;
};

/**
 * A ComboBox for selecting line patterns.
 */
class GuiExport PrefLinePattern : public PrefComboBox
{
  Q_OBJECT
public:
  explicit PrefLinePattern ( QWidget * parent = nullptr );
  ~PrefLinePattern() override;
protected:
  void changeEvent(QEvent*) override;
  void updateLanguage();
  QMetaType::Type getParamType() const override;
};

/**
 * The PrefCheckBox class.
 * \author Werner Mayer
 */
class GuiExport PrefCheckBox : public QCheckBox, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefCheckBox ( QWidget * parent = nullptr );
  ~PrefCheckBox() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  bool m_Default;
};

/**
 * The PrefRadioButton class.
 * \author Werner Mayer
 */
class GuiExport PrefRadioButton : public QRadioButton, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefRadioButton ( QWidget * parent = nullptr );
  ~PrefRadioButton() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  bool m_Default;
};

/**
 * The PrefSlider class.
 * \author Werner Mayer
 */
class GuiExport PrefSlider : public QSlider, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefSlider ( QWidget * parent = nullptr );
  ~PrefSlider() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  int m_Default;
};

/**
 * The PrefColorButton class.
 * \author Werner Mayer
 */
class GuiExport PrefColorButton : public ColorButton, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefColorButton ( QWidget * parent = nullptr );
  ~PrefColorButton() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  QColor m_Default;
};

/** The PrefUnitSpinBox class.
 * \author wandererfan
 * a simple Unit aware spin box.
 * See also \ref PrefQuantitySpinBox
 */
class GuiExport PrefUnitSpinBox : public QuantitySpinBox, public PrefWidget
{
    Q_OBJECT

    Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
    Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
    explicit PrefUnitSpinBox ( QWidget * parent = nullptr );
    ~PrefUnitSpinBox() override;

    void setEntryName( const QByteArray& name ) override;
    void setAutoSave(bool enable) override;

protected:
    // restore from/save to parameters
    void restorePreferences() override;
    void savePreferences() override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
  double m_Default;
};

class PrefQuantitySpinBoxPrivate;

/**
 * The PrefQuantitySpinBox class.
 * \author Werner Mayer
 */
class GuiExport PrefQuantitySpinBox : public PrefUnitSpinBox
{
    Q_OBJECT

    Q_PROPERTY(int historySize READ historySize WRITE setHistorySize)

public:
    explicit PrefQuantitySpinBox (QWidget * parent = nullptr);
    ~PrefQuantitySpinBox() override;

    /// set the input field to the last used value (works only if the setParamGrpPath() was called)
    void setToLastUsedValue();
    /// get the value of the history size property
    int historySize() const;
    /// set the value of the history size property
    void setHistorySize(int);

    /** @name history and default management */
    //@{
    /// push a new value to the history, if no string given the actual text of the input field is used.
    void pushToHistory();
    /// get the history of the field, newest first
    QStringList getHistory() const;
    //@}

    void setParamGrpPath( const QByteArray& path ) override;

protected:
    void contextMenuEvent(QContextMenuEvent * event) override;
    void restorePreferences() override;
    void savePreferences() override;

private:
    int _historySize;
    Q_DISABLE_COPY(PrefQuantitySpinBox)
};

/** The PrefFontBox class.
 * \author wandererfan
 */
class GuiExport PrefFontBox : public QFontComboBox, public PrefWidget
{
  Q_OBJECT

  Q_PROPERTY( QByteArray prefEntry READ entryName     WRITE setEntryName     ) // clazy:exclude=qproperty-without-notify
  Q_PROPERTY( QByteArray prefPath  READ paramGrpPath  WRITE setParamGrpPath  ) // clazy:exclude=qproperty-without-notify

public:
  explicit PrefFontBox ( QWidget * parent = nullptr );
  ~PrefFontBox() override;
  void setAutoSave(bool enable) override;

protected:
  // restore from/save to parameters
  void restorePreferences() override;
  void savePreferences() override;

private:
  QFont m_Default;
};

} // namespace Gui

ENABLE_BITMASK_OPERATORS(Gui::PrefWidget::EntryFlag)

#endif // GUI_PREFWIDGETS_H
