#include "DocmalaPlugin.h"
#include "DocmalaConstants.h"

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/rightpane.h>

#include <coreplugin/editormanager/editormanager.h>

#include <texteditor/texteditor.h>


#include <QAction>
#include <QMessageBox>
#include <QMainWindow>
#include <QMenu>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QMutexLocker>

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>
#include <texteditor/normalindenter.h>
#include <texteditor/texteditorconstants.h>
#include <projectexplorer/taskhub.h>

#include <utils/styledbar.h>

#include "OptionsPage.h"

#include <docmala/Docmala.h>
#include <docmala/HtmlOutput.h>

namespace docmala {
namespace Internal {




DocmalaPlugin::DocmalaPlugin()
{
    // Create your members
}

DocmalaPlugin::~DocmalaPlugin()
{
    // Unregister objects from the plugin manager's object pool
    // Delete members
    _renderThread.quit();
    _renderThread.wait(200);
    if( _renderThread.isRunning() ) {
        _renderThread.terminate();
    }
}

bool DocmalaPlugin::initialize(const QStringList &arguments, QString *errorString)
{
    // Register objects in the plugin manager's object pool
    // Load settings
    // Add actions to menus
    // Connect to other plugins' signals
    // In the initialize function, a plugin can be sure that the plugins it
    // depends on have initialized their members.
    Q_UNUSED(arguments)
    Q_UNUSED(errorString)

    ProjectExplorer::TaskHub::addCategory(Constants::DOCMALA_TASK_ID, "Documentation errors", true);

    _renderThread.setObjectName("docmalaRenderer");
    _renderThread.start();
    _renderTimer.moveToThread(&_renderThread);
    _renderTimer.setSingleShot(true);
    connect( &_renderTimer, &QTimer::timeout, this, &DocmalaPlugin::render, Qt::DirectConnection);

    _settings.load(Core::ICore::settings());
    
    createOptionsPage();

    QAction *action = new QAction(tr("Preview"), this);
    Core::Command *cmd = Core::ActionManager::registerAction(action, Constants::Docmala_ID,
                                                             Core::Context(Core::Constants::C_GLOBAL));
    cmd->setDefaultKeySequence(QKeySequence(tr("Ctrl+Meta+X")));

    connect(action, &QAction::triggered,
            [=]() {
        _showPreviewPane = !_showPreviewPane;
        showPreview(_showPreviewPane);
    });

    Core::ActionContainer *menu = Core::ActionManager::createMenu(Constants::Docmala_MENU_ID);
    menu->menu()->setTitle(tr("Docmala"));
    menu->addAction(cmd);
    Core::ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, [this] { _settings.save(Core::ICore::settings()); });

    connect(Core::EditorManager::EditorManager::instance(), &Core::EditorManager::EditorManager::currentEditorChanged, [this] {
        auto document = Core::EditorManager::instance()->currentDocument();

        //Core::EditorManager::instance()->currentEditor()->s
        disconnect(_documentChangedConnection);
        _document = document;
        _documentChangedConnection = connect( document, &Core::IDocument::contentsChanged, this, &DocmalaPlugin::updatePreview );
        documentChanged();
        auto editor = TextEditor::BaseTextEditor::currentTextEditor();
        if( editor != nullptr )
             connect( editor->editorWidget(), &QPlainTextEdit::cursorPositionChanged, [this] {
                updateHighLight();
             });

    });
    //&DocmalaPlugin::updatePreview);

    return true;
}

void DocmalaPlugin::extensionsInitialized()
{
    // Retrieve objects from the plugin manager's object pool
    // In the extensionsInitialized function, a plugin can be sure that all
    // plugins that depend on it are completely initialized.

    _previewPane = new QWidget;
    {
        QVBoxLayout *layout = new QVBoxLayout(_previewPane);
        layout->setMargin(0);
        layout->setSpacing(0);

        auto toolBar = new Utils::StyledBar(_previewPane);
        auto m_toolBarLayout = new QHBoxLayout(toolBar);
        m_toolBarLayout->setMargin(0);
        m_toolBarLayout->setSpacing(0);

        m_toolBarLayout->addStretch();
        layout->addWidget(toolBar);

        _preview = new QWebEngineView (nullptr);
        _preview->setPage (new QWebEnginePage());
        connect(_preview->page(), &QWebEnginePage::loadStarted, [this] {_pageIsLoaded = false;});
        connect(_preview->page(), &QWebEnginePage::loadFinished, [this] {_pageIsLoaded = true;});
        _preview->setStyleSheet (QStringLiteral ("QWebEngineView {background: #FFFFFF;}"));

        layout->addWidget(_preview);
    }
}

ExtensionSystem::IPlugin::ShutdownFlag DocmalaPlugin::aboutToShutdown()
{
    // Save settings
    // Disconnect from signals that are not needed during shutdown
    // Hide UI (if you add UI that is not in the main window directly)
    return SynchronousShutdown;
}

void DocmalaPlugin::triggerAction()
{
    QMessageBox::information(Core::ICore::mainWindow(),
                             tr("Action Triggered"),
                             tr("This is an action from Docmala."));
}

void DocmalaPlugin::createOptionsPage()
{
    auto page = new OptionsPage(_settings);
    addAutoReleasedObject(page);
    connect(page, &OptionsPage::settingsChanged, this, &DocmalaPlugin::settingsChanged);
}

void DocmalaPlugin::settingsChanged()
{
    _settings.save(Core::ICore::settings());
}

void DocmalaPlugin::documentChanged()
{
    if( !_document )
        return;

    if( !_document->filePath().endsWith(".dml") ) {
        showPreview(false);
        return;
    } else {
        showPreview(_showPreviewPane);
    }

    if( !_showPreviewPane )
        return;
}

void DocmalaPlugin::updatePreview()
{
    QMutexLocker locker(&_renderDataMutex);
    _renderContent = _document->contents();
    _renderFileName = _document->filePath().toString();
    auto editor = TextEditor::BaseTextEditor::currentTextEditor();
    if( editor ) {
        _renderCurrentLine = editor->editorWidget()->textCursor().blockNumber()+1;
    } else {
         _renderCurrentLine = -1;
    }
    _renderPreviewFollowCursor = _previewFollowCursor;
    _renderPreviewHighlightLine = _previewHighlightLine;
    QMetaObject::invokeMethod(&_renderTimer, "start", Qt::QueuedConnection);
}

void DocmalaPlugin::updateHighLight()
{
    if( !_pageIsLoaded )
        return;
    auto editor = TextEditor::BaseTextEditor::currentTextEditor();
    if( editor ) {
        if( _previewHighlightLine ) {
           _preview->page()->runJavaScript(QString("highlightLine(") + QString::number(editor->editorWidget()->textCursor().blockNumber()+1) + ")");
        }

        if( _previewFollowCursor ) {
            _preview->page()->runJavaScript(QString("scrollToLine(") + QString::number(editor->editorWidget()->textCursor().blockNumber()+1) + ")");
        }
    }
}

void DocmalaPlugin::render()
{
    std::string fileName;
    std::string content;

    {
        QMutexLocker locker(&_renderDataMutex);
        fileName = _renderFileName.toStdString();
        content = _renderContent.toStdString();
    }

    if( _renderLastFileName != fileName ) {
        _docmala.reset(new docmala::Docmala(_settings.docmalaInstallDir().path().toStdString() + "/plugins"));
    }
    _renderLastFileName = fileName;

    _docmala->parseData(content, fileName);

    HtmlOutput htmlOutput;
    ParameterList parameters;
    parameters.insert(std::make_pair("embedImages", Parameter{"embedImages", "", FileLocation() } ));
    std::string scripts = "var lastStyle;" "\n"
                          "var lastElement = null;" "\n"
    "function highlightLine(line) { " "\n"
    "    if( lastElement ) lastElement.style = lastStyle;" "\n"
    "    var myElement = document.querySelector(\"#line_\"+line.toString());" "\n"
    "    if( myElement ) {" "\n"
    "        lastStyle = myElement.style;" "\n"
    "        myElement.style.border = \"2px solid grey\";" "\n"
    "        myElement.style.borderRadius = \"6px\";" "\n"
    "        myElement.style.background = \"lightgrey\";" "\n"
    "    }" "\n"
    "    lastElement = myElement;" "\n"
    "}" "\n"
    "function scrollToLine(line) { " "\n"
    "    var myElement = document.querySelector(\"#line_\"+line.toString());" "\n"
    "    if( myElement ) {" "\n"
    "        const elementRect = myElement.getBoundingClientRect();" "\n"
    "        const absoluteElementTop = elementRect.top + window.pageYOffset;" "\n"
    "        const middle = absoluteElementTop - (window.innerHeight / 2);" "\n"
    "        window.scrollTo(0, middle);" "\n"
    "    }" "\n"
    "    lastElement = myElement;" "\n"
    "}" "\n";

    scripts += "function editCurrentLine() { " "\n";
    if( _renderPreviewFollowCursor && _renderCurrentLine != -1 ) {
        scripts += "scrollToLine("+std::to_string(_renderCurrentLine)+");" "\n";
    }
    if( _renderPreviewHighlightLine && _renderCurrentLine != -1 ) {
        scripts += "highlightLine("+std::to_string(_renderCurrentLine)+");" "\n";
    }
    scripts += "}" "\n";

    scripts +=
    "if (window.addEventListener)" "\n"
    "    window.addEventListener(\"load\", editCurrentLine, false);" "\n"
    "else if (window.attachEvent)" "\n"
    "    window.attachEvent(\"onload\", editCurrentLine);" "\n"
    "else window.onload = editCurrentLine;" "\n"
    ;

    QString html = QString::fromStdString(htmlOutput.produceHtml(parameters, _docmala->document(), scripts));
    QFile f("/home/michael/test.html");
    f.open(QIODevice::WriteOnly);
    f.write(html.toLatin1());
    f.close();
    {
        QMutexLocker locker(&_renderDataMutex);
        _renderRenderedHTML = html;
        _renderOccuredErrors = _docmala->errors();
    }

    QMetaObject::invokeMethod(this, "renderingFinished", Qt::QueuedConnection);
}

void DocmalaPlugin::showPreview(bool show)
{
    if (Core::RightPaneWidget::instance()->isShown() != show) {
        if (show) {
            Core::RightPaneWidget::instance()->setWidget(_previewPane);
            Core::RightPaneWidget::instance()->setShown(true);
            updatePreview();
        }
        else {
            Core::RightPaneWidget::instance()->setWidget(0);
            Core::RightPaneWidget::instance()->setShown(false);
        }
    }
}

void DocmalaPlugin::renderingFinished()
{
    QMutexLocker locker(&_renderDataMutex);
    _preview->page()->setHtml( _renderRenderedHTML, QUrl() );
     ProjectExplorer::TaskHub::clearTasks(Constants::DOCMALA_TASK_ID);
    for( auto error : _renderOccuredErrors ) {
        ProjectExplorer::TaskHub::addTask(ProjectExplorer::Task(
                                              ProjectExplorer::Task::Error,
                                              QString::fromStdString(error.message),
                                              Utils::FileName::fromString(QString::fromStdString(error.location.fileName)),
                                              error.location.line,
                                              Constants::DOCMALA_TASK_ID));
        for( auto extended : error.extendedInformation ) {
            ProjectExplorer::TaskHub::addTask(ProjectExplorer::Task(
                                                  ProjectExplorer::Task::Unknown,
                                                  QString("    ") + QString::fromStdString(extended.message),
                                                  Utils::FileName::fromString(QString::fromStdString(extended.location.fileName)),
                                                  extended.location.line,
                                                  Constants::DOCMALA_TASK_ID));
        }
    }

    Core::EditorManager::activateEditor(Core::EditorManager::currentEditor());

}

} // namespace Internal
} // namespace Docmala