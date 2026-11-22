#include "TerminalPanel.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QTextBlock>
#include "Diagnostics.h"
#include "Icons.h"
#include "Settings.h"
#include "Theme.h"

// ---------------------------------------------------------------- helpers

namespace {
// ANSI colour indexes 0-7 and 8-15 (bright): one set tuned for dark themes, one for light themes
const QColor kAnsiDark[16] = {
    QColor(0x3c, 0x3c, 0x3c), QColor(0xf4, 0x7a, 0x6b), QColor(0x7f, 0xd0, 0x8a), QColor(0xe5, 0xc0, 0x7b),
    QColor(0x6e, 0xa2, 0xff), QColor(0xc6, 0x78, 0xdd), QColor(0x56, 0xc2, 0xd0), QColor(0xd4, 0xd4, 0xd4),
    QColor(0x7a, 0x7a, 0x7a), QColor(0xff, 0x9a, 0x8c), QColor(0x9a, 0xe6, 0xa4), QColor(0xf2, 0xd3, 0x95),
    QColor(0x92, 0xbc, 0xff), QColor(0xd9, 0x9a, 0xee), QColor(0x7a, 0xd7, 0xe3), QColor(0xff, 0xff, 0xff)};
const QColor kAnsiLight[16] = {
    QColor(0x3c, 0x3c, 0x3c), QColor(0xcd, 0x31, 0x31), QColor(0x1a, 0x7f, 0x37), QColor(0x9a, 0x67, 0x00),
    QColor(0x05, 0x50, 0xae), QColor(0xa6, 0x26, 0xa4), QColor(0x0b, 0x72, 0x85), QColor(0x55, 0x5b, 0x63),
    QColor(0x6e, 0x77, 0x81), QColor(0xd1, 0x24, 0x2f), QColor(0x2e, 0x9a, 0x4a), QColor(0xb0, 0x7d, 0x00),
    QColor(0x09, 0x69, 0xda), QColor(0x82, 0x50, 0xdf), QColor(0x1b, 0x8a, 0x9e), QColor(0x1f, 0x23, 0x28)};

const QColor& ansi16(int i) { return (Theme::isDark() ? kAnsiDark : kAnsiLight)[i < 0 ? 0 : i > 15 ? 15 : i]; }

QColor ansi256(int n) {
    if (n < 16) return ansi16(n);
    if (n >= 232) { const int g = 8 + (n - 232) * 10; return QColor(g, g, g); }
    n -= 16;
    auto lvl = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
    return QColor(lvl(n / 36), lvl((n / 6) % 6), lvl(n % 6));
}

bool isWindowsShell(const QString& shell) {
    const QString b = QFileInfo(shell).completeBaseName().toLower();
    return b == "cmd";
}
}  // namespace

void TerminalPanel::shellInvocation(const QString& shellIn, const QString& line, QString* program, QStringList* args, QString* nativeArgs) {
    QString shell = shellIn.trimmed();
    if (nativeArgs) nativeArgs->clear();
    if (shell.isEmpty()) {
#ifdef Q_OS_WIN
        shell = qEnvironmentVariable("COMSPEC", "cmd.exe");
#else
        shell = qEnvironmentVariable("SHELL");
        if (shell.isEmpty()) shell = "/bin/sh";
#endif
    }
    *program = shell;
    args->clear();
    const QString base = QFileInfo(shell).completeBaseName().toLower();
    if (isWindowsShell(shell)) {
        // cmd needs its command line passed unquoted-by-Qt, or "a b" gets mangled
        if (nativeArgs) *nativeArgs = "/d /s /c \"" + line + "\"";
        else *args << "/d" << "/s" << "/c" << line;
    } else if (base == "powershell" || base == "pwsh") {
        *args << "-NoLogo" << "-NoProfile" << "-Command" << line;
    } else {
        *args << "-c" << line;
    }
}

void TerminalPanel::appendAnsi(QPlainTextEdit* edit, const QString& textIn) {
    QString text = textIn;
    text.replace("\r\n", "\n");
    static const QRegularExpression esc("\x1b(?:\\[([0-9;?]*)([ -/]*[@-~])|\\][^\x07\x1b]*(?:\x07|\x1b\\\\)|[@-Z\\\\-_])");
    QTextCursor cur(edit->document());
    cur.movePosition(QTextCursor::End);
    QTextCharFormat fmt = cur.charFormat();
    fmt.clearForeground();                           // plain text uses the terminal's own colour, so it follows the theme
    fmt.setFontWeight(QFont::Normal);
    auto put = [&](QString s) {
        s.remove('\r');
        s.remove(QChar(0x07));
        s.remove(QChar(0x08));
        if (!s.isEmpty()) cur.insertText(s, fmt);
    };
    int pos = 0;
    QRegularExpressionMatchIterator it = esc.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        put(text.mid(pos, m.capturedStart() - pos));
        pos = m.capturedEnd();
        if (m.captured(2) != "m") continue;                     // only colours / bold are kept
        const QStringList parts = m.captured(1).split(';');
        for (int i = 0; i < parts.size(); ++i) {
            const int n = parts[i].isEmpty() ? 0 : parts[i].toInt();
            if (n == 0) { fmt.clearForeground(); fmt.setBackground(Qt::transparent); fmt.setFontWeight(QFont::Normal); fmt.setFontUnderline(false); }
            else if (n == 1) fmt.setFontWeight(QFont::Bold);
            else if (n == 22) fmt.setFontWeight(QFont::Normal);
            else if (n == 4) fmt.setFontUnderline(true);
            else if (n == 24) fmt.setFontUnderline(false);
            else if (n >= 30 && n <= 37) fmt.setForeground(ansi16(n - 30 + (n == 30 ? 8 : 0)));
            else if (n == 39) fmt.clearForeground();
            else if (n >= 90 && n <= 97) fmt.setForeground(ansi16(n - 90 + 8));
            else if (n >= 40 && n <= 47) fmt.setBackground(Theme::isDark() ? ansi16(n - 40).darker(n == 40 ? 100 : 160) : ansi16(n - 40).lighter(n == 40 ? 175 : 250));
            else if (n == 49) fmt.setBackground(Qt::transparent);
            else if ((n == 38 || n == 48) && i + 2 < parts.size() && parts[i + 1] == "5") {
                const QColor c = ansi256(parts[i + 2].toInt());
                if (n == 38) fmt.setForeground(c); else fmt.setBackground(c);
                i += 2;
            } else if ((n == 38 || n == 48) && i + 4 < parts.size() && parts[i + 1] == "2") {
                const QColor c(parts[i + 2].toInt(), parts[i + 3].toInt(), parts[i + 4].toInt());
                if (n == 38) fmt.setForeground(c); else fmt.setBackground(c);
                i += 4;
            }
        }
    }
    put(text.mid(pos));
}

// ---------------------------------------------------------------- construction

TerminalPanel::TerminalPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("terminalPanel");
    setMinimumHeight(110);
    cwd_ = QDir::homePath();
    previousCwd_ = cwd_;

    QVBoxLayout* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // header
    QWidget* head = new QWidget(this);
    head->setObjectName("terminalHeader");
    head->setFixedHeight(32);
    QHBoxLayout* h = new QHBoxLayout(head);
    h->setContentsMargins(12, 0, 6, 0);
    h->setSpacing(8);
    // (no "TERMINAL" title: the tab above already says Terminal)
    chip_ = new QLabel(head);
    chip_->setObjectName("terminalChip");
    chip_->setFixedHeight(20);
    cwdLabel_ = new QLabel(head);
    cwdLabel_->setObjectName("terminalCwd");
    cwdLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(chip_, 0, Qt::AlignVCenter);
    h->addSpacing(4);
    h->addWidget(cwdLabel_, 1);
    auto button = [head](Icons::Glyph g, const QString& tip) {
        QToolButton* b = new QToolButton(head);
        b->setIcon(Icons::get(g));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(26, 26);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    stopBtn_ = button(Icons::Stop, "Stop the running program (Ctrl+C)");
    stopBtn_->setEnabled(false);
    QToolButton* clear = button(Icons::Trash, "Clear (Ctrl+L)");
    QToolButton* hide = button(Icons::Close, "Hide terminal (Ctrl+`)");
    h->addWidget(stopBtn_);
    h->addWidget(clear);
    h->addWidget(hide);
    v->addWidget(head);

    // output
    out_ = new QPlainTextEdit(this);
    out_->setObjectName("terminalOutput");
    out_->setReadOnly(true);
    out_->setFrameShape(QFrame::NoFrame);
    out_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    out_->setUndoRedoEnabled(false);
    out_->setFocusPolicy(Qt::ClickFocus);
    out_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    out_->viewport()->setMouseTracking(true);          // hover underlines "file.cpp:12" references
    out_->viewport()->installEventFilter(this);
    v->addWidget(out_, 1);

    // prompt row
    QWidget* row = new QWidget(this);
    row->setObjectName("terminalPromptRow");
    QHBoxLayout* r = new QHBoxLayout(row);
    r->setContentsMargins(10, 0, 10, 0);
    r->setSpacing(6);
    prompt_ = new QLabel(row);
    prompt_->setObjectName("terminalPrompt");
    input_ = new QLineEdit(row);
    input_->setObjectName("terminalInput");
    input_->setPlaceholderText("Type a command and press Enter");
    input_->installEventFilter(this);
    r->addWidget(prompt_);
    r->addWidget(input_, 1);
    v->addWidget(row);

    connect(input_, &QLineEdit::returnPressed, this, &TerminalPanel::submit);
    connect(stopBtn_, &QToolButton::clicked, this, &TerminalPanel::stop);
    connect(clear, &QToolButton::clicked, this, &TerminalPanel::clearScreen);
    connect(hide, &QToolButton::clicked, this, &TerminalPanel::hideRequested);
    connect(&AppSettings::instance(), &AppSettings::changed, this, &TerminalPanel::applySettings);
    setFocusProxy(input_);

    applySettings();
    writeNote("DevPad terminal. Type a command; Ctrl+C stops a running program, Ctrl+L clears the screen.", Theme::Dim);
    refreshPrompt();
}

TerminalPanel::~TerminalPanel() {
    if (proc_) {
        proc_->disconnect(this);
        if (proc_->state() != QProcess::NotRunning) {
            proc_->kill();
            proc_->waitForFinished(1500);
        }
    }
}

void TerminalPanel::applySettings() {
    const AppSettings::Values& s = AppSettings::instance().values();
    const QFont f = Theme::codeFont(s.terminalFontSize, s.fontFamily);
    out_->setFont(f);
    input_->setFont(f);
    prompt_->setFont(f);
    out_->setMaximumBlockCount(s.terminalScrollback);
    chip_->setText(shellName());
    refreshPrompt();
}

QString TerminalPanel::shellName() const {
    QString program;
    QStringList args;
    shellInvocation(AppSettings::instance().values().terminalShell, QString(), &program, &args, nullptr);
    return QFileInfo(program).completeBaseName();
}

void TerminalPanel::focusInput() { input_->setFocus(); }

QString TerminalPanel::plainText() const { return out_->toPlainText(); }

// ---------------------------------------------------------------- prompt, history, keys

static QString shortDir(const QString& dir) {
    QString p = QDir::toNativeSeparators(dir);
    const QString home = QDir::toNativeSeparators(QDir::homePath());
    if (p.compare(home, Qt::CaseInsensitive) == 0) return "~";
    if (p.startsWith(home + QDir::separator(), Qt::CaseInsensitive)) p = "~" + p.mid(home.size());
    return p;
}

void TerminalPanel::refreshPrompt() {
    const QString name = QFileInfo(cwd_).fileName();
    prompt_->setText((name.isEmpty() ? shortDir(cwd_) : name) + QString::fromUtf8(" \xe2\x9d\xaf"));     // "name ❯"
    cwdLabel_->setText(shortDir(cwd_));
    cwdLabel_->setToolTip(QDir::toNativeSeparators(cwd_));
    const bool busy = isRunning();
    chip_->setObjectName(busy ? "terminalChipBusy" : "terminalChip");
    chip_->setText(busy ? shellName() + " - running" : shellName());
    chip_->style()->unpolish(chip_);
    chip_->style()->polish(chip_);
    stopBtn_->setEnabled(busy);
    input_->setPlaceholderText(busy ? "Running - what you type goes to the program (Ctrl+C stops it)" : "Type a command and press Enter");
}

void TerminalPanel::historyStep(int dir) {
    if (history_.isEmpty()) return;
    if (historyPos_ == history_.size()) draft_ = input_->text();
    historyPos_ = std::max(0, std::min(static_cast<int>(history_.size()), historyPos_ + dir));
    input_->setText(historyPos_ == history_.size() ? draft_ : history_[historyPos_]);
}

// ---------------------------------------------------------------- clickable file references

bool TerminalPanel::refInBlock(int block, int column, QString* path, int* line, int* col, int* from, int* to) const {
    const QTextBlock b = out_->document()->findBlockByNumber(block);
    if (!b.isValid()) return false;
    const QString text = b.text();
    if (column < 0 || column >= text.size()) return false;
    const QByteArray bytes = text.toUtf8();
    FileRef r;                                             // the engine works on UTF-8 bytes: convert the column there and back
    if (!fileRefAt(bytes.toStdString(), static_cast<size_t>(text.left(column).toUtf8().size()), r)) return false;
    QString p = QString::fromStdString(r.path);
    if (!QFileInfo(p).isAbsolute()) p = QDir(cwd_).filePath(p);
    if (path) *path = QDir::cleanPath(p);
    if (line) *line = static_cast<int>(r.line);
    if (col) *col = static_cast<int>(r.col);
    if (from) *from = b.position() + static_cast<int>(QString::fromUtf8(bytes.left(static_cast<int>(r.start))).size());
    if (to) *to = b.position() + static_cast<int>(QString::fromUtf8(bytes.left(static_cast<int>(r.end))).size());
    return true;
}

bool TerminalPanel::linkAt(const QPoint& pos, QString* path, int* line, int* col, int* from, int* to) const {
    const QTextCursor c = out_->cursorForPosition(pos);
    // cursorForPosition gives the nearest gap between characters: also try the character left of it
    if (refInBlock(c.blockNumber(), c.positionInBlock(), path, line, col, from, to)) return true;
    return c.positionInBlock() > 0 && refInBlock(c.blockNumber(), c.positionInBlock() - 1, path, line, col, from, to) &&
           out_->cursorRect(c).left() >= pos.x() - out_->fontMetrics().horizontalAdvance(QLatin1Char('M'));
}

bool TerminalPanel::openLinkAt(int block, int column) {
    QString path;
    int line = 0, col = 0;
    if (!refInBlock(block, column, &path, &line, &col, nullptr, nullptr)) return false;
    emit openLocationRequested(path, line, col);
    return true;
}

void TerminalPanel::setHoverLink(int from, int to) {
    if (from == hoverFrom_) return;
    hoverFrom_ = from;
    QList<QTextEdit::ExtraSelection> sel;
    if (from >= 0) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(out_->document());
        s.cursor.setPosition(from);
        s.cursor.setPosition(to, QTextCursor::KeepAnchor);
        s.format.setFontUnderline(true);
        s.format.setForeground(Theme::Accent);
        sel << s;
    }
    out_->setExtraSelections(sel);
    out_->viewport()->setCursor(from >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
    out_->viewport()->setToolTip(from >= 0 ? QStringLiteral("Click to open") : QString());
}

bool TerminalPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == out_->viewport()) {
        if (e->type() == QEvent::MouseMove) {
            const QMouseEvent* m = static_cast<QMouseEvent*>(e);
            int from = -1, to = -1;
            if (!(m->buttons() & Qt::LeftButton) && linkAt(m->pos(), nullptr, nullptr, nullptr, &from, &to)) setHoverLink(from, to);
            else setHoverLink(-1, -1);
        } else if (e->type() == QEvent::Leave) {
            setHoverLink(-1, -1);
        } else if (e->type() == QEvent::MouseButtonRelease) {
            const QMouseEvent* m = static_cast<QMouseEvent*>(e);
            QString path;
            int line = 0, col = 0;
            // a plain click opens it; a drag that selected text stays a selection
            if (m->button() == Qt::LeftButton && !out_->textCursor().hasSelection() && linkAt(m->pos(), &path, &line, &col))
                emit openLocationRequested(path, line, col);
        }
        return false;
    }
    if (o == input_ && e->type() == QEvent::KeyPress) {
        QKeyEvent* k = static_cast<QKeyEvent*>(e);
        const bool ctrl = k->modifiers() & Qt::ControlModifier;
        if (k->key() == Qt::Key_Up && !ctrl) { historyStep(-1); return true; }
        if (k->key() == Qt::Key_Down && !ctrl) { historyStep(+1); return true; }
        if (ctrl && k->key() == Qt::Key_L) { clearScreen(); return true; }
        if (ctrl && k->key() == Qt::Key_C && !input_->hasSelectedText()) { stop(); input_->clear(); return true; }
        if (k->key() == Qt::Key_Escape && !isRunning()) { emit hideRequested(); return true; }
    }
    return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------- running things

void TerminalPanel::setWorkingDirectory(const QString& dir) {
    const QFileInfo fi(dir);
    if (!fi.isDir()) return;
    const QString d = fi.canonicalFilePath();
    if (d == cwd_) return;
    previousCwd_ = cwd_;
    cwd_ = d;
    refreshPrompt();
    emit directoryChanged(cwd_);
}

void TerminalPanel::writeOut(const QString& text) {
    if (text.isEmpty()) return;
    appendAnsi(out_, text);
    needNewline_ = !text.endsWith('\n');
    out_->verticalScrollBar()->setValue(out_->verticalScrollBar()->maximum());
}

void TerminalPanel::writeNote(const QString& text, const QColor& color) {
    if (needNewline_) writeOut("\n");
    QTextCursor cur(out_->document());
    cur.movePosition(QTextCursor::End);
    QTextCharFormat f;
    f.setForeground(color);
    cur.insertText(text + "\n", f);
    needNewline_ = false;
    out_->verticalScrollBar()->setValue(out_->verticalScrollBar()->maximum());
}

void TerminalPanel::echoCommand(const QString& line) {
    if (needNewline_) writeOut("\n");
    QTextCursor cur(out_->document());
    cur.movePosition(QTextCursor::End);
    QTextCharFormat p, t;
    p.setForeground(Theme::Accent);
    p.setFontWeight(QFont::DemiBold);
    t.setForeground(Theme::Heading);
    const QString name = QFileInfo(cwd_).fileName();
    cur.insertText((name.isEmpty() ? shortDir(cwd_) : name) + QString::fromUtf8(" \xe2\x9d\xaf "), p);
    cur.insertText(line + "\n", t);
    needNewline_ = false;
}

void TerminalPanel::clearScreen() {
    setHoverLink(-1, -1);
    out_->clear();
    needNewline_ = false;
}

void TerminalPanel::submit() {
    const QString line = input_->text();
    input_->clear();
    runCommand(line);
}

void TerminalPanel::runCommand(const QString& lineIn) {
    const QString line = lineIn;
    if (isRunning()) {                                  // input for the running program
        echoCommand(line);
        proc_->write((line + "\n").toLocal8Bit());
        return;
    }
    if (line.trimmed().isEmpty()) { echoCommand(QString()); return; }
    if (history_.isEmpty() || history_.last() != line) history_ << line;
    historyPos_ = history_.size();
    draft_.clear();
    echoCommand(line);
    if (builtin(line.trimmed())) return;
    start(line);
}

// cd / clear / exit are done here: a separate process cannot change our folder
bool TerminalPanel::builtin(const QString& line) {
    const QString lower = line.toLower();
    if (lower == "clear" || lower == "cls") { clearScreen(); return true; }
    if (lower == "exit" || lower == "quit") { emit hideRequested(); return true; }
    QString arg;
    bool isCd = false;
    if (lower == "cd" || lower == "chdir") { isCd = true; }
    else if (lower.startsWith("cd ") || lower.startsWith("cd\t") || lower.startsWith("chdir ")) { isCd = true; arg = line.mid(line.indexOf(QRegularExpression("\\s"))).trimmed(); }
#ifdef Q_OS_WIN
    else if (line.size() == 2 && line[1] == ':' && line[0].isLetter()) { isCd = true; arg = line; }
#endif
    if (!isCd) return false;
    if (arg.compare("/d", Qt::CaseInsensitive) == 0) arg.clear();
    if (arg.startsWith("/d ", Qt::CaseInsensitive)) arg = arg.mid(3).trimmed();
    if ((arg.startsWith('"') && arg.endsWith('"') && arg.size() >= 2) || (arg.startsWith('\'') && arg.endsWith('\'') && arg.size() >= 2))
        arg = arg.mid(1, arg.size() - 2);
    if (arg.isEmpty()) {
#ifdef Q_OS_WIN
        writeNote(QDir::toNativeSeparators(cwd_), Theme::Syntax[0]);       // cmd: a bare "cd" shows the folder
        return true;
#else
        arg = QDir::homePath();
#endif
    }
    if (arg == "-") arg = previousCwd_;
    if (arg == "~" || arg.startsWith("~/") || arg.startsWith("~\\")) arg = QDir::homePath() + arg.mid(1);
    const QString target = QFileInfo(arg).isAbsolute() ? arg : QDir(cwd_).filePath(arg);
    const QFileInfo fi(target);
    if (!fi.isDir()) {
        writeNote("cd: no such folder: " + arg, Theme::Error);
        return true;
    }
    setWorkingDirectory(fi.canonicalFilePath());
    return true;
}

void TerminalPanel::start(const QString& line) {
    if (proc_) { proc_->disconnect(this); proc_->deleteLater(); }
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setWorkingDirectory(cwd_);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    proc_->setProcessEnvironment(env);

    connect(proc_, &QProcess::readyRead, this, [this]() { writeOut(QString::fromLocal8Bit(proc_->readAll())); });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &TerminalPanel::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        writeNote("Could not start '" + shellName() + "'. Check the shell in Settings > Terminal.", Theme::Error);
        refreshPrompt();
    });

    QString program, native;
    QStringList args;
    shellInvocation(AppSettings::instance().values().terminalShell, line, &program, &args, &native);
#ifdef Q_OS_WIN
    if (!native.isEmpty()) proc_->setNativeArguments(native);
#endif
    proc_->start(program, args);
    refreshPrompt();
}

void TerminalPanel::onFinished(int code, QProcess::ExitStatus status) {
    writeOut(QString::fromLocal8Bit(proc_->readAll()));
    const bool ok = status == QProcess::NormalExit && code == 0;
    if (status == QProcess::CrashExit) writeNote("[stopped]", Theme::Warning);
    else if (code != 0) writeNote(QString("[exit code %1]").arg(code), Theme::Error);
    if (!ok) queue_.clear();
    refreshPrompt();
    if (!queue_.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            if (queue_.isEmpty() || isRunning()) return;
            const QString next = queue_.takeFirst();
            echoCommand(next);
            start(next);
        });
    }
}

void TerminalPanel::runSequence(const QStringList& lines) {
    if (lines.isEmpty()) return;
    queue_.clear();
    if (isRunning()) {                                  // F5 again: restart
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(2000);
        writeNote("[stopped]", Theme::Warning);
    }
    QStringList rest = lines;
    const QString first = rest.takeFirst();
    queue_ = rest;
    echoCommand(first);
    start(first);
}

void TerminalPanel::stop() {
    queue_.clear();
    if (!isRunning()) return;
    proc_->terminate();
    QTimer::singleShot(1200, proc_, [p = QPointer<QProcess>(proc_)]() { if (p && p->state() != QProcess::NotRunning) p->kill(); });
}
