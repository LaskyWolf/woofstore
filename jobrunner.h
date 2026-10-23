#pragma once
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QThread>

// A package operation that outlives the store. The job runs in its own session with its output in a file, so closing or crashing the
// store cannot cut a pacman transaction in half (a closed pipe would kill it with SIGPIPE). The store reads the file and, when it is
// started again, finds the job still running or already finished.
//
// Files in the job folder: job.json (title and step names), job.pid (the runner), job.log (all output), job.exit (exit code, written last).
enum class JobState { None, Running, Finished, Interrupted };
struct JobInfo { JobState state = JobState::None; int exitCode = 0; QString title; QStringList steps; };

// $1 folder, then the program and its arguments. systemd-inhibit stops a shutdown or suspend from landing in the middle of the job.
inline QString jobRunnerScript() {
    return "dir=$1; shift; trap '' HUP\n"
           "echo $$ > \"$dir/job.pid\"\n"
           "if command -v systemd-inhibit >/dev/null 2>&1 && systemd-inhibit --what=sleep --mode=delay true >/dev/null 2>&1; then\n"
           "  systemd-inhibit --what=shutdown:sleep --who=WoofStore --why='WoofStore is changing packages' --mode=block \"$@\" </dev/null >>\"$dir/job.log\" 2>&1\n"
           "else \"$@\" </dev/null >>\"$dir/job.log\" 2>&1; fi\n"
           "echo $? > \"$dir/job.exit.tmp\" && mv \"$dir/job.exit.tmp\" \"$dir/job.exit\"\n";
}

inline QByteArray readSmallFile(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }

inline bool runnerAlive(const QString &dir) {
    const qint64 pid = readSmallFile(dir + "/job.pid").trimmed().toLongLong();
    return pid > 0 && readSmallFile(QString("/proc/%1/cmdline").arg(pid)).contains("woof-store-job");
}

inline JobInfo readJob(const QString &dir) {
    JobInfo info;
    const QJsonObject meta = QJsonDocument::fromJson(readSmallFile(dir + "/job.json")).object();
    if (meta.isEmpty()) return info;
    info.title = meta.value("title").toString();
    for (const auto &name : meta.value("steps").toArray()) info.steps << name.toString();
    const QByteArray exitCode = readSmallFile(dir + "/job.exit").trimmed();
    if (!exitCode.isEmpty()) { info.state = JobState::Finished; info.exitCode = exitCode.toInt(); }
    else if (runnerAlive(dir)) info.state = JobState::Running;
    else if (!QFile::exists(dir + "/job.pid") && QFileInfo(dir + "/job.json").lastModified().secsTo(QDateTime::currentDateTime()) < 5) info.state = JobState::Running;  // Just launched: the runner has not written its pid yet
    else info.state = JobState::Interrupted;
    return info;
}

inline void clearJob(const QString &dir) {
    for (const char *name : {"job.json", "job.pid", "job.log", "job.exit", "job.exit.tmp"}) QFile::remove(dir + "/" + name);
}

// Starts the job and returns at once. The environment (SUDO_ASKPASS and the like) is the one the job should see.
inline bool launchJob(const QString &dir, const QString &title, const QStringList &steps, const QString &program, const QStringList &arguments,
                      const QProcessEnvironment &environment, const QString &cwd, QString *error) {
    QDir().mkpath(dir);
    clearJob(dir);
    QJsonArray names; for (const auto &name : steps) names.append(name);
    QSaveFile meta(dir + "/job.json");
    const QByteArray json = QJsonDocument(QJsonObject{{"title", title}, {"steps", names}}).toJson(QJsonDocument::Compact);
    if (!meta.open(QIODevice::WriteOnly) || meta.write(json) != json.size() || !meta.commit()) { if (error) *error = "Cannot write in " + dir; return false; }
    QProcess runner;
    runner.setProgram("setsid");
    runner.setArguments(QStringList{"-f", "bash", "-c", jobRunnerScript(), "woof-store-job", dir, program} + arguments);
    runner.setProcessEnvironment(environment);
    if (!cwd.isEmpty()) runner.setWorkingDirectory(cwd);
    if (!runner.startDetached()) { clearJob(dir); if (error) *error = "Cannot start setsid: " + runner.errorString(); return false; }
    return true;
}

// Reads a running job's output as it grows and says when it is over.
class JobWatcher {
public:
    explicit JobWatcher(QString folder) : dir(std::move(folder)) { age.start(); }
    static constexpr int stillRunning = -2000;
    // Output written since the last call. Only whole lines (or progress-bar redraws) are returned, so a multi-byte character is never cut.
    QString readNew(bool everything = false) {
        QFile file(dir + "/job.log");
        if (!file.open(QIODevice::ReadOnly) || !file.seek(position)) return {};
        const QByteArray data = file.readAll();
        qsizetype cut = everything ? data.size() : qMax(data.lastIndexOf('\n'), data.lastIndexOf('\r')) + 1;
        position += cut;
        return QString::fromUtf8(data.left(cut));
    }
    // stillRunning, or the exit code (-1: the job vanished without writing one).
    int poll() {
        const QByteArray exitCode = readSmallFile(dir + "/job.exit").trimmed();
        if (!exitCode.isEmpty()) return exitCode.toInt();
        if (QFile::exists(dir + "/job.pid") ? !runnerAlive(dir) : age.elapsed() > 8000) return QFile::exists(dir + "/job.exit") ? readSmallFile(dir + "/job.exit").trimmed().toInt() : -1;
        return stillRunning;
    }
private:
    QString dir; qint64 position = 0; QElapsedTimer age;
};
