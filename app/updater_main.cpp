#include "update_transaction.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QSaveFile>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>
#endif

// Keep the public executable path present even across a crash between backup and replacement.
static bool replaceFile(const QString &source, const QString &target) {
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<const wchar_t *>(source.utf16()),
        reinterpret_cast<const wchar_t *>(target.utf16()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    return ::rename(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0;
#endif
}
static bool backupFile(const QString &source, const QString &target) {
    if (QFile::exists(target)) return false;
    QFile input(source); QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const auto bytes = input.read(256 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return false;
    }
    return output.setPermissions(input.permissions()) && output.commit();
}
static bool alive(qint64 pid) {
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool result = WaitForSingleObject(h, 0) == WAIT_TIMEOUT; CloseHandle(h); return result;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}
static QProcessEnvironment launchEnvironment(const QString &journal, bool rollback) {
    auto env = QProcessEnvironment::systemEnvironment();
    // These refer to the old package's mounted/extracted runtime, which may have disappeared.
    for (const auto &key : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "APPIMAGE", "APPDIR",
                           "OWD", "ARGV0", "GST_PLUGIN_PATH_1_0", "GST_PLUGIN_SCANNER_1_0", "PIPEWIRE_MODULE_DIR", "SPA_PLUGIN_DIR",
                           "LAZARUS_LAUNCHER_PATH", "LAZARUS_LAUNCHER_PID", "LAZARUS_UPDATE_JOB"}) env.remove(key);
    env.remove("LAZARUS_UPDATE_TRANSACTION"); env.remove("LAZARUS_UPDATE_ROLLBACK");
    if (rollback) env.insert("LAZARUS_UPDATE_ROLLBACK", "A atualização não foi concluída. A versão anterior foi restaurada com seus dados preservados.");
    else env.insert("LAZARUS_UPDATE_TRANSACTION", journal);
    return env;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("LazarusLabs"); app.setApplicationName("LazarusShare");
    if (app.arguments().size() != 2) return 1;
    const auto journal = QFileInfo(app.arguments()[1]).absoluteFilePath();
    auto o = Updates::readJson(journal);
    if (!Updates::validTransaction(journal, o)) return 1;
    const auto dir = QFileInfo(journal).absolutePath();
    QLockFile lock(dir + "/helper.lock"); if (!lock.tryLock()) return 1;
    const auto target = o["target"].toString(), staged = o["staged"].toString(), backup = o["backup"].toString();
    const auto size = qint64(o["size"].toDouble()); const auto hash = o["sha256"].toString().toLatin1();
    auto state = [&](const QString &value) { o["state"] = value; return Updates::writeJson(journal, o); };
    auto finish = [&] {
        if (!state("completed")) return false;
        QFile::remove(backup); QFile::remove(staged); return true;
    };
    auto arguments = [&] { return o["extract"].toBool() ? QStringList{"--appimage-extract-and-run"} : QStringList{}; };
    auto restartOld = [&] {
        QProcess old; old.setProgram(target); old.setArguments(arguments()); old.setWorkingDirectory(QFileInfo(target).absolutePath());
        old.setProcessEnvironment(launchEnvironment(journal, true)); return old.startDetached();
    };
    if (o["state"] == "completed") { QFile::remove(backup); QFile::remove(staged); return 0; }
    // Signal readiness only after taking ownership and validating the staged download.
    const bool invalidStage = o["state"] == "prepared" && !Updates::verifyFile(staged, size, hash);
    if (invalidStage && !o["recovering"].toBool()) {
        if (QFile::exists(backup)) return 1;
        finish(); return 1;
    }
    if (!Updates::writeJson(dir + "/ready.json", {{"id", o["id"]}})) return 1;
    auto aborted = [&] { return Updates::readJson(dir + "/abort.json")["id"] == o["id"]; };
    QElapsedTimer wait; wait.start();
    while (alive(qint64(o["appPid"].toDouble())) || alive(qint64(o["launcherPid"].toDouble()))) {
        if (aborted() && !QFile::exists(backup)) { finish(); return 0; }
        if (wait.elapsed() >= 60000) return 1; // No executable has been touched.
        QThread::msleep(50);
    }
    if (aborted() && !QFile::exists(backup)) { finish(); return 0; }
    if (invalidStage && !QFile::exists(backup)) { finish(); return restartOld() ? 0 : 1; }
    const auto ack = Updates::readJson(dir + "/ack.json");
    if (ack["id"] == o["id"] && ack["version"] == o["version"] && Updates::verifyFile(target, size, hash)) {
        if (!finish()) return 1;
        if (o["recovering"].toBool()) {
            QProcess current; auto environment = launchEnvironment(journal, false);
            environment.remove("LAZARUS_UPDATE_TRANSACTION"); current.setProcessEnvironment(environment);
            current.setProgram(target); current.setArguments(arguments()); current.setWorkingDirectory(QFileInfo(target).absolutePath());
            return current.startDetached() ? 0 : 1;
        }
        return 0;
    }
    // An interrupted transaction with a backup is always rolled back rather than guessing success.
    if (o["recovering"].toBool() && QFile::exists(backup)) {
        if (QFileInfo(target).isSymLink() || QFileInfo(backup).isSymLink()) return 1;
        if (!state("rollingBack") || !replaceFile(backup, target)) return 1;
        if (!finish()) return 1;
        return restartOld() ? 0 : 1;
    }
    if (o["state"] == "rollingBack" && !QFile::exists(backup)) {
        if (!QFile::exists(target) || !finish()) return 1;
        return restartOld() ? 0 : 1;
    }
    if (!QFile::exists(backup)) {
        if (!Updates::verifyFile(staged, size, hash) || !QFile::exists(target)) return 1;
        if (!state("replacing")) return 1;
        if (!backupFile(target, backup)) { finish(); return restartOld() ? 0 : 1; }
    }
    auto restore = [&] {
        if (QFileInfo(target).isSymLink() || QFileInfo(backup).isSymLink()) return false;
        if (!state("rollingBack") || !replaceFile(backup, target) || !finish()) return false;
        return restartOld();
    };
    if (!Updates::verifyFile(staged, size, hash)) return restore() ? 0 : 1;
    if (!replaceFile(staged, target)) return restore() ? 0 : 1;
    if (!state("installed")) return restore() ? 0 : 1;
    QProcess child;
    auto env = launchEnvironment(journal, false);
#ifdef Q_OS_WIN
    const auto jobName = QString("Local\\LazarusUpdate-%1").arg(o["id"].toString());
    HANDLE job = CreateJobObjectW(nullptr, reinterpret_cast<const wchar_t *>(jobName.utf16()));
    if (!job) return restore() ? 0 : 1;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { CloseHandle(job); return restore() ? 0 : 1; }
    env.insert("LAZARUS_UPDATE_JOB", jobName);
#else
    const auto owner = getpid();
    child.setChildProcessModifier([owner] {
        setsid(); prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != owner) _exit(1);
    });
#endif
    child.setProcessEnvironment(env); child.setWorkingDirectory(QFileInfo(target).absolutePath());
    child.setProgram(target); child.setArguments(arguments());
    child.setStandardOutputFile(QProcess::nullDevice()); child.setStandardErrorFile(QProcess::nullDevice());
    child.start();
    bool confirmed = false;
    if (child.waitForStarted(10000)) {
        o["childPid"] = double(child.processId());
        if (!state("launched")) {
#ifdef Q_OS_WIN
            TerminateJobObject(job, 1); CloseHandle(job);
#else
            kill(-pid_t(child.processId()), SIGKILL);
#endif
            child.kill(); child.waitForFinished(10000); return restore() ? 0 : 1;
        }
        wait.restart();
        while (wait.elapsed() < 60000 && child.state() != QProcess::NotRunning) {
            const auto confirmation = Updates::readJson(dir + "/ack.json");
            if (confirmation["id"] == o["id"] && confirmation["version"] == o["version"]) { confirmed = true; break; }
            child.waitForFinished(50);
        }
    }
    if (confirmed) {
#ifdef Q_OS_WIN
        limits.BasicLimitInformation.LimitFlags = 0;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) confirmed = false;
#endif
    }
    if (confirmed) {
        finish();
        // Keep ownership until exit. QProcess's destructor would otherwise kill the running app.
        while (child.state() != QProcess::NotRunning) child.waitForFinished(1000);
#ifdef Q_OS_WIN
        CloseHandle(job);
#endif
        return 0;
    }
#ifdef Q_OS_WIN
    TerminateJobObject(job, 1); CloseHandle(job);
#else
    if (child.processId() > 0) kill(-pid_t(child.processId()), SIGKILL);
#endif
    child.kill(); if (!child.waitForFinished(10000) && child.state() != QProcess::NotRunning) return 1;
    return restore() ? 0 : 1;
}
