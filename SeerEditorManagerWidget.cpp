#include "SeerEditorManagerWidget.h"
#include "SeerEditorWidget.h"
#include "SeerEditorOptionsBarWidget.h"
#include "SeerUtl.h"
#include <QtWidgets/QToolButton>
#include <QtWidgets/QFileDialog>
#include <QtCore/QString>
#include <QtCore/QTextStream>
#include <QtCore/QFile>
#include <QtCore/QDebug>

SeerEditorManagerWidget::SeerEditorManagerWidget (QWidget* parent) : QWidget(parent) {

    // Initialize private data

    // Setup UI
    setupUi(this);

    // Setup the widgets
    tabWidget->setMovable(true);
    tabWidget->setTabsClosable(true);

    SeerEditorOptionsBarWidget* editorOptionsBar = new SeerEditorOptionsBarWidget(tabWidget);

    tabWidget->setCornerWidget(editorOptionsBar, Qt::TopRightCorner);

    // Create a place holder tab with a special name of "".
    createEditorWidgetTab("", "");

    // Connect things.
    QObject::connect(tabWidget,                                 &QTabWidget::tabCloseRequested,    this, &SeerEditorManagerWidget::handleTabCloseRequested);
    QObject::connect(editorOptionsBar->fileOpenToolButton(),    &QToolButton::clicked,             this, &SeerEditorManagerWidget::handleFileOpenToolButtonClicked);
    QObject::connect(editorOptionsBar->textSearchToolButton(),  &QToolButton::clicked,             this, &SeerEditorManagerWidget::handleTextSearchToolButtonClicked);
}

SeerEditorManagerWidget::~SeerEditorManagerWidget () {
}

void SeerEditorManagerWidget::dumpEntries () const {

    qDebug() << __PRETTY_FUNCTION__ << ":";

    SeerEditorManagerEntries::const_iterator b = beginEntry();
    SeerEditorManagerEntries::const_iterator e = endEntry();

    while (b != e) {
        qDebug() << __PRETTY_FUNCTION__ << ":" << "\tFullname:" << b->fullname << "File:" << b->file;
        b++;
    }
}

bool SeerEditorManagerWidget::hasEntry (const QString& fullname) const {

    if (_entries.find(fullname) != _entries.end()) {
        return true;
    }

    return false;
}

SeerEditorManagerEntries::iterator SeerEditorManagerWidget::addEntry (const QString& fullname, const QString& file) {

    SeerEditorManagerEntry entry;

    entry.fullname = fullname;
    entry.file     = file;
    entry.widget   = 0;

    return _entries.insert(fullname, entry);
}

SeerEditorManagerEntries::iterator SeerEditorManagerWidget::findEntry (const QString& fullname) {
    return _entries.find(fullname);
}

SeerEditorManagerEntries::const_iterator SeerEditorManagerWidget::findEntry (const QString& fullname) const {
    return _entries.find(fullname);
}

SeerEditorManagerEntries::iterator SeerEditorManagerWidget::beginEntry () {
    return _entries.begin();
}

SeerEditorManagerEntries::const_iterator SeerEditorManagerWidget::beginEntry () const {
    return _entries.begin();
}

SeerEditorManagerEntries::iterator SeerEditorManagerWidget::endEntry () {
    return _entries.end();
}

SeerEditorManagerEntries::const_iterator SeerEditorManagerWidget::endEntry () const {
    return _entries.end();
}

void SeerEditorManagerWidget::deleteEntry (SeerEditorManagerEntries::iterator i) {
    _entries.erase(i);
}

void SeerEditorManagerWidget::handleText (const QString& text) {

    if (text.startsWith("*stopped,reason=\"end-stepping-range\"")) {

        // *stopped,
        //
        // reason="end-stepping-range",
        //
        // frame={addr="0x0000000000400b45",
        //        func="main",
        //        args=[{name="argc",value="1"},{name="argv",value="0x7fffffffd5b8"}],
        //        file="helloworld.cpp",
        //        fullname="/home/erniep/Development/Peak/src/Seer/helloworld/helloworld.cpp",
        //        line="7",
        //        arch="i386:x86-64"},
        //
        // thread-id="1",
        // stopped-threads="all",
        // core="6"

        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString frame_text = Seer::parseFirst(newtext, "frame=", '{', '}', false);

        if (frame_text == "") {
            return;
        }

        QString fullname_text = Seer::parseFirst(frame_text, "fullname=", '"', '"', false);
        QString file_text     = Seer::parseFirst(frame_text, "file=",     '"', '"', false);
        QString line_text     = Seer::parseFirst(frame_text, "line=",     '"', '"', false);

        //qDebug() << __PRETTY_FUNCTION__ << ":" << frame_text;
        //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname_text << file_text << line_text;

        // If there is no file to open, just exit.
        if (fullname_text == "") {
            return;
        }

        // Get the EditorWidget for the file. Create one if needed.
        SeerEditorWidget* editorWidget = editorWidgetTab(fullname_text);

        if (editorWidget == 0) {
            editorWidget = createEditorWidgetTab(fullname_text, file_text, text);
        }

        // Push this tab to the top.
        tabWidget->setCurrentWidget(editorWidget);

        // Give the EditorWidget the command text (read file, set line number, etc.).
        editorWidget->sourceArea()->handleText(text);

        return;

    }else if (text.startsWith("*stopped,reason=\"breakpoint-hit\"")) {

        // *stopped,
        //
        // reason="breakpoint-hit",
        //
        // disp="del",
        // bkptno="1",
        //
        // frame={addr="0x0000000000400b07",
        //        func="main",
        //        args=[{name="argc",value="1"},{name="argv",value="0x7fffffffd5b8"}],
        //        file="helloworld.cpp",
        //        fullname="/home/erniep/Development/Peak/src/Seer/helloworld/helloworld.cpp",
        //        line="7",
        //        arch="i386:x86-64"},
        //
        // thread-id="1",
        // stopped-threads="all",
        // core="1"

        // Now parse the table and re-add the breakpoints.
        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString reason_text          = Seer::parseFirst(newtext,    "reason=",           '"', '"', false);
        QString disp_text            = Seer::parseFirst(newtext,    "disp=",             '"', '"', false);
        QString bkptno_text          = Seer::parseFirst(newtext,    "bkptno=",           '"', '"', false);
        QString frame_text           = Seer::parseFirst(newtext,    "frame=",            '{', '}', false);
        QString thread_id_text       = Seer::parseFirst(newtext,    "thread-id=",        '"', '"', false);
        QString stopped_threads_text = Seer::parseFirst(newtext,    "stopped-threads=",  '"', '"', false);
        QString core_text            = Seer::parseFirst(newtext,    "core=",             '"', '"', false);

        if (frame_text == "") {
            return;
        }

        QString addr_text            = Seer::parseFirst(frame_text, "addr=",             '"', '"', false);
        QString func_text            = Seer::parseFirst(frame_text, "func=",             '"', '"', false);
        QString args_text            = Seer::parseFirst(frame_text, "args=",             '[', ']', false);
        QString file_text            = Seer::parseFirst(frame_text, "file=",             '"', '"', false);
        QString fullname_text        = Seer::parseFirst(frame_text, "fullname=",         '"', '"', false);
        QString line_text            = Seer::parseFirst(frame_text, "line=",             '"', '"', false);
        QString arch_text            = Seer::parseFirst(frame_text, "arch=",             '"', '"', false);

        // If there is no file to open, just exit.
        if (fullname_text == "") {
            return;
        }

        // Get the EditorWidget for the file. Create one if needed.
        SeerEditorWidget* editorWidget = editorWidgetTab(fullname_text);

        if (editorWidget == 0) {
            editorWidget = createEditorWidgetTab(fullname_text, file_text, text);
        }

        // Push this tab to the top.
        tabWidget->setCurrentWidget(editorWidget);

        // Give the EditorWidget the command text (read file, set line number, etc.).
        editorWidget->sourceArea()->handleText(text);

        // Ask for the breakpoint list to be resent, in case the encountered breakpoint was temporary.
        if (disp_text == "del") {
            emit refreshBreakpointsList();
        }

        return;

    }else if (text.startsWith("*stopped,reason=\"function-finished\"")) {

        // *stopped,
        //
        // reason="function-finished",
        //
        // frame={addr="0x0000000000400b40",
        //        func="main",
        //        args=[{name="argc",value="1"},{name="argv",value="0x7fffffffd5b8"}],
        //        file="helloworld.cpp",
        //        fullname="/home/erniep/Development/Peak/src/Seer/helloworld/helloworld.cpp",
        //        line="11",
        //        arch="i386:x86-64"},
        //
        // thread-id="1",
        // stopped-threads="all",
        // core="6"

        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString frame_text = Seer::parseFirst(newtext, "frame=", '{', '}', false);

        if (frame_text == "") {
            return;
        }

        QString fullname_text = Seer::parseFirst(frame_text, "fullname=", '"', '"', false);
        QString file_text     = Seer::parseFirst(frame_text, "file=",     '"', '"', false);
        QString line_text     = Seer::parseFirst(frame_text, "line=",     '"', '"', false);

        //qDebug() << __PRETTY_FUNCTION__ << ":" << frame_text;
        //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname_text << file_text << line_text;

        // If there is no file to open, just exit.
        if (fullname_text == "") {
            return;
        }

        // Get the EditorWidget for the file. Create one if needed.
        SeerEditorWidget* editorWidget = editorWidgetTab(fullname_text);

        if (editorWidget == 0) {
            editorWidget = createEditorWidgetTab(fullname_text, file_text, text);
        }

        // Push this tab to the top.
        tabWidget->setCurrentWidget(editorWidget);

        // Give the EditorWidget the command text (read file, set line number, etc.).
        editorWidget->sourceArea()->handleText(text);

        return;

    }else if (text.startsWith("*stopped,reason=\"location-reached\"")) {

        // *stopped,
        //
        // reason=\"location-reached\",
        //
        // frame={addr=\"0x0000000000400607\",
        //        func=\"main\",
        //        args=[],
        //        file=\"helloonefile.cpp\",
        //        fullname=\"/home/erniep/Development/Peak/src/Seer/helloonefile/helloonefile.cpp\",
        //        line=\"35\",
        //        arch=\"i386:x86-64\"},
        //
        // thread-id=\"1\",
        // stopped-threads=\"all\",
        // core=\"1\"

        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString frame_text = Seer::parseFirst(newtext, "frame=", '{', '}', false);

        if (frame_text == "") {
            return;
        }

        QString fullname_text = Seer::parseFirst(frame_text, "fullname=", '"', '"', false);
        QString file_text     = Seer::parseFirst(frame_text, "file=",     '"', '"', false);
        QString line_text     = Seer::parseFirst(frame_text, "line=",     '"', '"', false);

        //qDebug() << __PRETTY_FUNCTION__ << ":" << frame_text;
        //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname_text << file_text << line_text;

        // If there is no file to open, just exit.
        if (fullname_text == "" || file_text == "") {
            return;
        }

        // Get the EditorWidget for the file. Create one if needed.
        SeerEditorWidget* editorWidget = editorWidgetTab(fullname_text);

        if (editorWidget == 0) {
            editorWidget = createEditorWidgetTab(fullname_text, file_text, text);
        }

        // Push this tab to the top.
        tabWidget->setCurrentWidget(editorWidget);

        // Give the EditorWidget the command text (read file, set line number, etc.).
        editorWidget->sourceArea()->handleText(text);

        return;

    }else if (text.startsWith("*stopped,reason=\"signal-received\"")) {

        // *stopped,
        //
        // reason=\"signal-received\",
        // signal-name=\"SIGSEGV\",
        // signal-meaning=\"Segmentation fault\",
        //
        // frame={addr=\"0x00007ffff712a420\",
        //        func=\"raise\",
        //        args=[],
        //        from=\"/lib64/libc.so.6\",
        //        arch=\"i386:x86-64\"},
        //
        // thread-id=\"1\",
        // stopped-threads=\"all\",
        // core=\"6\"

        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString frame_text = Seer::parseFirst(newtext, "frame=", '{', '}', false);

        if (frame_text == "") {
            return;
        }

        QString fullname_text = Seer::parseFirst(frame_text, "fullname=", '"', '"', false);
        QString file_text     = Seer::parseFirst(frame_text, "file=",     '"', '"', false);
        QString line_text     = Seer::parseFirst(frame_text, "line=",     '"', '"', false);

        //qDebug() << __PRETTY_FUNCTION__ << ":" << frame_text;
        //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname_text << file_text << line_text;

        // If there is no file to open, just exit.
        if (fullname_text == "" || file_text == "") {
            return;
        }

        // Get the EditorWidget for the file. Create one if needed.
        SeerEditorWidget* editorWidget = editorWidgetTab(fullname_text);

        if (editorWidget == 0) {
            editorWidget = createEditorWidgetTab(fullname_text, file_text, text);
        }

        // Push this tab to the top.
        tabWidget->setCurrentWidget(editorWidget);

        // Give the EditorWidget the command text (read file, set line number, etc.).
        editorWidget->sourceArea()->handleText(text);

        return;

    }else if (text.startsWith("^done,BreakpointTable={") && text.endsWith("}")) {

        //
        // See SeerBreakpointsBrowserWidget.cpp
        //
        // ^done,BreakpointTable={
        //    ...
        // }
        //

        // We have a breakpoint table. Start by clearing all breakpoints
        // in the editor widgets that are opened.
        SeerEditorManagerEntries::iterator b = beginEntry();
        SeerEditorManagerEntries::iterator e = endEntry();

        while (b != e) {
            b->widget->sourceArea()->clearBreakpoints();
            b++;
        }

        // Now parse the table and re-add the breakpoints.
        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString body_text = Seer::parseFirst(newtext, "body=", '[', ']', false);

        //qDebug() << __PRETTY_FUNCTION__ << ":" << body_text;

        if (body_text != "") {

            QStringList bkpt_list = Seer::parse(newtext, "bkpt=", '{', '}', false);

            for ( const auto& bkpt_text : bkpt_list  ) {
                QString number_text            = Seer::parseFirst(bkpt_text, "number=",            '"', '"', false);
                QString type_text              = Seer::parseFirst(bkpt_text, "type=",              '"', '"', false);
                QString disp_text              = Seer::parseFirst(bkpt_text, "disp=",              '"', '"', false);
                QString enabled_text           = Seer::parseFirst(bkpt_text, "enabled=",           '"', '"', false);
                QString addr_text              = Seer::parseFirst(bkpt_text, "addr=",              '"', '"', false);
                QString func_text              = Seer::parseFirst(bkpt_text, "func=",              '"', '"', false);
                QString file_text              = Seer::parseFirst(bkpt_text, "file=",              '"', '"', false);
                QString fullname_text          = Seer::parseFirst(bkpt_text, "fullname=",          '"', '"', false);
                QString line_text              = Seer::parseFirst(bkpt_text, "line=",              '"', '"', false);
                QString thread_groups_text     = Seer::parseFirst(bkpt_text, "thread-groups=",     '[', ']', false);
                QString times_text             = Seer::parseFirst(bkpt_text, "times=",             '"', '"', false);
                QString original_location_text = Seer::parseFirst(bkpt_text, "original-location=", '"', '"', false);

                SeerEditorManagerEntries::iterator i = findEntry(fullname_text);
                SeerEditorManagerEntries::iterator e = endEntry();

                if (i != e) {
                    i->widget->sourceArea()->addBreakpoint(number_text.toInt(), line_text.toInt(), (enabled_text == "y" ? true : false));
                }
            }
        }

    }else if (text.startsWith("^done,stack=[") && text.endsWith("]")) {

        //qDebug() << __PRETTY_FUNCTION__ << ":" << text;

        //
        // See SeerStackFramesBrowserWidget.cpp
        // ^done,stack=[
        //     ...
        // ]
        //

        //qDebug() << __PRETTY_FUNCTION__ << ":" << text;

        // Now parse the table and re-add the breakpoints.
        QString newtext = Seer::filterEscapes(text); // Filter escaped characters.

        QString stack_text = Seer::parseFirst(newtext, "stack=", '[', ']', false);

        if (stack_text != "") {

            // Clear current lines in all opened editor widgets.
            SeerEditorManagerEntries::iterator b = beginEntry();
            SeerEditorManagerEntries::iterator e = endEntry();

            while (b != e) {
              //b->widget->setCurrentLine(0);
                b->widget->sourceArea()->clearCurrentLines();
                b++;
            }

            // Parse through the frame list and set the current lines that are in the frame list.
            QStringList frame_list = Seer::parse(newtext, "frame=", '{', '}', false);

            for ( const auto& frame_text : frame_list  ) {
                QString level_text    = Seer::parseFirst(frame_text, "level=",    '"', '"', false);
                QString addr_text     = Seer::parseFirst(frame_text, "addr=",     '"', '"', false);
                QString func_text     = Seer::parseFirst(frame_text, "func=",     '"', '"', false);
                QString file_text     = Seer::parseFirst(frame_text, "file=",     '"', '"', false);
                QString fullname_text = Seer::parseFirst(frame_text, "fullname=", '"', '"', false);
                QString line_text     = Seer::parseFirst(frame_text, "line=",     '"', '"', false);
                QString arch_text     = Seer::parseFirst(frame_text, "arch=",     '"', '"', false);

                SeerEditorManagerEntries::iterator i = findEntry(fullname_text);
                SeerEditorManagerEntries::iterator e = endEntry();

                if (i != e) {
                    //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname_text << line_text;
                    i->widget->sourceArea()->addCurrentLine(line_text.toInt());
                }
            }
        }

    }else if (text.startsWith("^error,msg=\"No registers.\"")) {

        //qDebug() << __PRETTY_FUNCTION__ << ":" << text;

        // Clear current lines in all opened editor widgets.
        SeerEditorManagerEntries::iterator b = beginEntry();
        SeerEditorManagerEntries::iterator e = endEntry();

        while (b != e) {
            b->widget->sourceArea()->setCurrentLine(0);
            b++;
        }

    }else if (text.contains(QRegExp("^([0-9]+)\\^done,value="))) {

        // 10^done,value="1"
        // 11^done,value="0x7fffffffd538"

        QWidget* w = tabWidget->currentWidget();

        if (w) {
            static_cast<SeerEditorWidget*>(w)->sourceArea()->handleText(text);
        }

    }else if (text.contains(QRegExp("^([0-9]+)\\^error,msg="))) {

        // 12^error,msg="No symbol \"return\" in current context."
        // 13^error,msg="No symbol \"cout\" in current context."

        QWidget* w = tabWidget->currentWidget();

        if (w) {
            static_cast<SeerEditorWidget*>(w)->sourceArea()->handleText(text);
        }

    }else{
        // Ignore others.
        return;
    }
}

void SeerEditorManagerWidget::handleTabCloseRequested (int index) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << index << tabWidget->count() << tabWidget->tabText(0);

    // If it is the place holder, don't delete it.
    if (tabWidget->tabText(index) == "") {
        return;
    }

    // Delete the tab.
    deleteEditorWidgetTab(index);

    // If there are no tabs left, create a place holder.
    if (tabWidget->count() == 0) {
        createEditorWidgetTab("", "");
    }
}

void SeerEditorManagerWidget::handleOpenFile (const QString& file, const QString& fullname, int lineno) {

    // Must have a valid filename.
    if (file == "" || fullname == "") {
        return;
    }

    // Get the EditorWidget for the file. Create one if needed.
    SeerEditorWidget* editorWidget = editorWidgetTab(fullname);

    if (editorWidget == 0) {
        editorWidget = createEditorWidgetTab(fullname, file);
    }

    // Push this tab to the top.
    tabWidget->setCurrentWidget(editorWidget);

    // If lineno is > 0, set the line number of the editor widget
    if (lineno > 0) {
        editorWidget->sourceArea()->scrollToLine(lineno);
    }

    // Ask for the breakpoint list to be resent, in case this file has breakpoints.
    emit refreshBreakpointsList();

    // Ask for the stackframe list to be resent, in case this file has currently executing lines.
    emit refreshStackFrames();
}

SeerEditorWidget* SeerEditorManagerWidget::currentEditorWidgetTab () {

    QWidget* w = tabWidget->currentWidget();

    if (w == 0) {
        return 0;
    }

    return (SeerEditorWidget*) w;
}

SeerEditorWidget* SeerEditorManagerWidget::editorWidgetTab (const QString& fullname) {

    // Do we have an entry for 'fullname'?
    SeerEditorManagerEntries::iterator i = findEntry(fullname);

    if (i == endEntry()) {
        return 0;
    }

    return i->widget;
}

SeerEditorWidget* SeerEditorManagerWidget::createEditorWidgetTab (const QString& fullname, const QString& file, const QString& text) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname << file << text << tabWidget->count() << tabWidget->tabText(0);

    // Remove the place holder tab, if present.
    if (tabWidget->count() == 1 && tabWidget->tabText(0) == "") {
        deleteEditorWidgetTab(0);
    }

    // Create the Editor widget and add it to the tab.
    SeerEditorWidget* editorWidget = new SeerEditorWidget(this);

    tabWidget->addTab(editorWidget, file);

    // Connect signals.
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::insertBreakpoint,              this, &SeerEditorManagerWidget::handleInsertBreakpoint);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::deleteBreakpoints,             this, &SeerEditorManagerWidget::handleDeleteBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::enableBreakpoints,             this, &SeerEditorManagerWidget::handleEnableBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::disableBreakpoints,            this, &SeerEditorManagerWidget::handleDisableBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::runToLine,                     this, &SeerEditorManagerWidget::handleRunToLine);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::addVariableExpression,         this, &SeerEditorManagerWidget::handleAddVariableExpression);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::refreshVariableValues,         this, &SeerEditorManagerWidget::handleRefreshVariableValues);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::evaluateVariableExpression,    this, &SeerEditorManagerWidget::handleEvaluateVariableExpression);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::addMemoryVisualize,            this, &SeerEditorManagerWidget::handleAddMemoryVisualizer);

    // Send the Editor widget the command to load the file. ??? Do better than this.
    editorWidget->sourceArea()->handleText(text);

    // Add an entry to our table.
    SeerEditorManagerEntries::iterator i = addEntry(fullname, file);
    i->widget = editorWidget;

    // Return the editor widget.
    return i->widget;
}

SeerEditorWidget* SeerEditorManagerWidget::createEditorWidgetTab (const QString& fullname, const QString& file) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname << file << tabWidget->count() << tabWidget->tabText(0);

    // Remove the place holder tab, if present.
    if (tabWidget->count() == 1 && tabWidget->tabText(0) == "") {
        deleteEditorWidgetTab(0);
    }

    // Create the Editor widget and add it to the tab.
    SeerEditorWidget* editorWidget = new SeerEditorWidget(this);

    tabWidget->addTab(editorWidget, file);

    // Connect signals.
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::insertBreakpoint,              this, &SeerEditorManagerWidget::handleInsertBreakpoint);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::deleteBreakpoints,             this, &SeerEditorManagerWidget::handleDeleteBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::enableBreakpoints,             this, &SeerEditorManagerWidget::handleEnableBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::disableBreakpoints,            this, &SeerEditorManagerWidget::handleDisableBreakpoints);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::runToLine,                     this, &SeerEditorManagerWidget::handleRunToLine);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::addVariableExpression,         this, &SeerEditorManagerWidget::handleAddVariableExpression);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::refreshVariableValues,         this, &SeerEditorManagerWidget::handleRefreshVariableValues);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::evaluateVariableExpression,    this, &SeerEditorManagerWidget::handleEvaluateVariableExpression);
    QObject::connect(editorWidget->sourceArea(), &SeerEditorWidgetSourceArea::addMemoryVisualize,            this, &SeerEditorManagerWidget::handleAddMemoryVisualizer);

    // Load the file.
    editorWidget->sourceArea()->open(fullname, file);

    // Add an entry to our table.
    SeerEditorManagerEntries::iterator i = addEntry(fullname, file);
    i->widget = editorWidget;

    // Return the editor widget.
    return i->widget;
}

void SeerEditorManagerWidget::deleteEditorWidgetTab (int index) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << index << tabWidget->count() << tabWidget->tabText(index);

    // Get the editor widget.
    SeerEditorWidget* editorWidget = static_cast<SeerEditorWidget*>(tabWidget->widget(index));

    // Look for the matching entry for the EditorWidget.
    // If found, delete it and clean up the map.
    SeerEditorManagerEntries::iterator b = beginEntry();
    SeerEditorManagerEntries::iterator e = endEntry();

    while (b != e) {
        if (editorWidget == b->widget) {

            deleteEntry(b);                 // Delete the entry from the map.
            tabWidget->removeTab(index);    // Remove the tab.
            delete editorWidget;              // Delete the actual EditorWidget

            break;
        }

        b++;
    }
}

void SeerEditorManagerWidget::handleFileOpenToolButtonClicked () {

    QString filename = QFileDialog::getOpenFileName(this, tr("Open Source File"), "", tr("Source files (*.*)"));

    if (filename == "") {
        return;
    }

    QFileInfo info(filename);
    info.makeAbsolute();

    handleOpenFile(info.fileName(), info.filePath(), 0);
}

void SeerEditorManagerWidget::handleTextSearchToolButtonClicked () {

    SeerEditorWidget* w = currentEditorWidgetTab();

    if (w == 0) {
        return;
    }

    w->showSearchBar(true);
}

void SeerEditorManagerWidget::handleInsertBreakpoint (QString breakpoint) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << breakpoint;

    // rethrow
    emit insertBreakpoint (breakpoint);
}

void SeerEditorManagerWidget::handleDeleteBreakpoints (QString breakpoints) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << breakpoints;

    // rethrow
    emit deleteBreakpoints (breakpoints);
}

void SeerEditorManagerWidget::handleEnableBreakpoints (QString breakpoints) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << breakpoints;

    // rethrow
    emit enableBreakpoints (breakpoints);
}

void SeerEditorManagerWidget::handleDisableBreakpoints (QString breakpoints) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << breakpoints;

    // rethrow
    emit disableBreakpoints (breakpoints);
}

void SeerEditorManagerWidget::handleRunToLine (QString fullname, int lineno) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << fullname << lineno;

    // rethrow
    emit runToLine (fullname, lineno);
}

void SeerEditorManagerWidget::handleAddVariableExpression (QString expression) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << expression;

    // rethrow
    emit addVariableExpression (expression);
}

void SeerEditorManagerWidget::handleRefreshVariableValues () {

    //qDebug() << __PRETTY_FUNCTION__ << ":";

    // rethrow
    emit refreshVariableValues ();
}

void SeerEditorManagerWidget::handleEvaluateVariableExpression (int expressionid, QString expression) {

    //qDebug() << __PRETTY_FUNCTION__ << ":";

    // rethrow
    emit evaluateVariableExpression (expressionid, expression);
}

void SeerEditorManagerWidget::handleAddMemoryVisualizer (QString expression) {

    //qDebug() << __PRETTY_FUNCTION__ << ":" << expression;

    // rethrow
    emit addMemoryVisualize (expression);
}

