//! A package operation that outlives the store. The job runs in its own session with its output in a file, so closing
//! or crashing the store cannot cut a pacman transaction in half (a closed pipe would kill it with SIGPIPE). The store
//! reads the file and, when it is started again, finds the job still running or already finished.
//!
//! Files in the job folder: `job.json` (title and step names), `job.pid` (the runner), `job.log` (all output),
//! `job.exit` (exit code, written last).

use serde::{Deserialize, Serialize};
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, SystemTime};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum JobState {
    None,
    Running,
    Finished,
    Interrupted,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct JobInfo {
    pub state: JobState,
    pub exit_code: i32,
    pub title: String,
    pub steps: Vec<String>,
}

#[derive(Serialize, Deserialize)]
struct Meta {
    title: String,
    steps: Vec<String>,
}

/// `$1` folder, then the program and its arguments. systemd-inhibit stops a shutdown or suspend from landing in the
/// middle of the job.
pub fn runner_script() -> &'static str {
    "dir=$1; shift; trap '' HUP\n\
     echo $$ > \"$dir/job.pid\"\n\
     if command -v systemd-inhibit >/dev/null 2>&1 && systemd-inhibit --what=sleep --mode=delay true >/dev/null 2>&1; then\n\
     \x20 systemd-inhibit --what=shutdown:sleep --who=WoofStore --why='WoofStore is changing packages' --mode=block \"$@\" </dev/null >>\"$dir/job.log\" 2>&1\n\
     else \"$@\" </dev/null >>\"$dir/job.log\" 2>&1; fi\n\
     echo $? > \"$dir/job.exit.tmp\" && mv \"$dir/job.exit.tmp\" \"$dir/job.exit\"\n"
}

/// True when `/proc/<pid>/maps` lists a system file as "(deleted)": the program runs from a library or executable
/// that a package upgrade has replaced.
pub fn runs_replaced_files(maps: &str) -> bool {
    maps.lines().any(|line| line.ends_with(" (deleted)") && line.contains(" /usr/"))
}

fn read_small(path: &Path) -> Vec<u8> {
    fs::read(path).unwrap_or_default()
}

/// The runner's pid; None until it is written. `echo $$ > job.pid` creates the file empty before filling it.
fn runner_pid(dir: &Path) -> Option<i64> {
    String::from_utf8_lossy(&read_small(&dir.join("job.pid"))).trim().parse().ok().filter(|&pid| pid > 0)
}

fn runner_alive(dir: &Path) -> bool {
    runner_pid(dir).is_some_and(|pid| String::from_utf8_lossy(&read_small(Path::new(&format!("/proc/{pid}/cmdline")))).contains("woof-store-job"))
}

fn exit_code(dir: &Path) -> Option<i32> {
    // job.exit.tmp holds the code while `mv` renames it; bash runs that last `mv` in place of itself, so the runner can look gone first
    let read = |name: &str| String::from_utf8_lossy(&read_small(&dir.join(name))).trim().to_string();
    let text = Some(read("job.exit")).filter(|t| !t.is_empty()).unwrap_or_else(|| read("job.exit.tmp"));
    if text.is_empty() {
        None
    } else {
        Some(text.parse().unwrap_or(-1))
    }
}

pub fn read_job(dir: &Path) -> JobInfo {
    let mut info = JobInfo { state: JobState::None, exit_code: 0, title: String::new(), steps: Vec::new() };
    let Ok(meta) = serde_json::from_slice::<Meta>(&read_small(&dir.join("job.json"))) else { return info };
    info.title = meta.title;
    info.steps = meta.steps;
    let just_launched = || {
        // The runner has not written its pid yet
        fs::metadata(dir.join("job.json"))
            .and_then(|m| m.modified())
            .map(|time| SystemTime::now().duration_since(time).unwrap_or_default() < Duration::from_secs(5))
            .unwrap_or(false)
    };
    if let Some(code) = exit_code(dir) {
        info.state = JobState::Finished;
        info.exit_code = code;
    } else if runner_alive(dir) || (runner_pid(dir).is_none() && just_launched()) {
        info.state = JobState::Running;
    } else {
        info.state = JobState::Interrupted;
    }
    info
}

/// The output of the last job stays in `last-job.log` after the job's own files are cleared, so a failed update can
/// still be read afterwards.
pub fn keep_job_log(dir: &Path) {
    let log = dir.join("job.log");
    if log.exists() {
        let _ = fs::copy(log, dir.join("last-job.log"));
    }
}

pub fn clear_job(dir: &Path) {
    for name in ["job.json", "job.pid", "job.log", "job.exit", "job.exit.tmp"] {
        let _ = fs::remove_file(dir.join(name));
    }
}

/// What to run and how. `env` adds to the store's own environment (SUDO_ASKPASS and the like).
#[derive(Debug, Clone, Default)]
pub struct JobSpec {
    pub title: String,
    pub steps: Vec<String>,
    pub program: PathBuf,
    pub args: Vec<String>,
    pub env: Vec<(String, String)>,
    pub cwd: Option<PathBuf>,
}

/// The store's root helper, run with pkexec. Installed, polkit gives its password dialog the store's own messages; a build
/// that is not installed uses the copy in the source tree, and Plasma shows its generic pkexec prompt.
pub fn root_helper() -> String {
    const INSTALLED: &str = "/usr/lib/woofstore/woofstore-root";
    if Path::new(INSTALLED).is_file() {
        INSTALLED.into()
    } else {
        concat!(env!("CARGO_MANIFEST_DIR"), "/../packaging/woofstore-root").into()
    }
}

/// `pkexec` arguments that run one verb of the root helper; empty strings are left out.
pub fn root_args(verb_and_args: &[&str]) -> Vec<String> {
    std::iter::once(root_helper()).chain(verb_and_args.iter().filter(|a| !a.is_empty()).map(|a| a.to_string())).collect()
}

/// Starts the job and returns at once. Scripts reach the root helper as `pkexec "$WOOFSTORE_ROOT" <verb> ...`.
pub fn launch_job(dir: &Path, spec: &JobSpec) -> Result<(), String> {
    fs::create_dir_all(dir).map_err(|e| format!("Cannot create {}: {e}", dir.display()))?;
    clear_job(dir);
    let meta = serde_json::to_vec(&Meta { title: spec.title.clone(), steps: spec.steps.clone() }).expect("plain strings");
    let temporary = dir.join("job.json.tmp");
    fs::write(&temporary, meta).and_then(|_| fs::rename(&temporary, dir.join("job.json"))).map_err(|_| format!("Cannot write in {}", dir.display()))?;
    let mut runner = Command::new("setsid");
    runner.arg("-f").args(["bash", "-c", runner_script(), "woof-store-job"]).arg(dir).arg(&spec.program).args(&spec.args);
    runner.env("WOOFSTORE_ROOT", root_helper()).envs(spec.env.iter().map(|(k, v)| (k, v)));
    if let Some(cwd) = &spec.cwd {
        runner.current_dir(cwd);
    }
    // setsid -f forks the runner into its own session and exits at once, so waiting here does not wait for the job.
    match runner.status() {
        Ok(status) if status.success() => Ok(()),
        Ok(status) => {
            clear_job(dir);
            Err(format!("setsid ended with {status}"))
        }
        Err(e) => {
            clear_job(dir);
            Err(format!("Cannot start setsid: {e}"))
        }
    }
}

/// How a job looks from outside, polled by the store.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Poll {
    Running,
    /// The exit code; -1 when the job vanished without writing one.
    Ended(i32),
}

/// Reads a running job's output as it grows and says when it is over.
pub struct JobWatcher {
    dir: PathBuf,
    position: u64,
    age: Instant,
}

impl JobWatcher {
    pub fn new(dir: impl Into<PathBuf>) -> Self {
        Self { dir: dir.into(), position: 0, age: Instant::now() }
    }

    /// Output written since the last call. Only whole lines (or progress-bar redraws) are returned, so a multi-byte
    /// character is never cut; `everything` also returns the unfinished last line.
    pub fn read_new(&mut self, everything: bool) -> String {
        let Ok(mut file) = fs::File::open(self.dir.join("job.log")) else { return String::new() };
        let mut data = Vec::new();
        if file.seek(SeekFrom::Start(self.position)).is_err() || file.read_to_end(&mut data).is_err() {
            return String::new();
        }
        let cut = if everything { data.len() } else { data.iter().rposition(|&b| b == b'\n' || b == b'\r').map_or(0, |i| i + 1) };
        self.position += cut as u64;
        String::from_utf8_lossy(&data[..cut]).into_owned()
    }

    pub fn poll(&self) -> Poll {
        if let Some(code) = exit_code(&self.dir) {
            return Poll::Ended(code);
        }
        let gone = if runner_pid(&self.dir).is_some() { !runner_alive(&self.dir) } else { self.age.elapsed() > Duration::from_secs(8) };
        if gone {
            // The runner may have written the exit code between the two reads
            return Poll::Ended(exit_code(&self.dir).unwrap_or(-1));
        }
        Poll::Running
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread::sleep;

    fn wait(watcher: &JobWatcher) -> Poll {
        let start = Instant::now();
        while watcher.poll() == Poll::Running && start.elapsed() < Duration::from_secs(15) {
            sleep(Duration::from_millis(50));
        }
        watcher.poll()
    }

    fn sh(title: &str, script: &str) -> JobSpec {
        JobSpec { title: title.into(), steps: vec!["A".into(), "B".into()], program: "sh".into(), args: vec!["-c".into(), script.into()], ..Default::default() }
    }

    #[test]
    fn job_runs_in_own_session_with_output_in_a_file() {
        let root = tempfile::tempdir().unwrap();
        let job = root.path().join("job");
        assert_eq!(read_job(&job).state, JobState::None);
        launch_job(&job, &sh("Test job", "echo out; echo err >&2; readlink /proc/$$/fd/1; ps -o sid= -p $$ | tr -d ' '; sleep 1; exit 3")).unwrap();
        let mut watcher = JobWatcher::new(&job);
        assert_eq!(wait(&watcher), Poll::Ended(3));
        let done = read_job(&job);
        assert_eq!((done.state, done.exit_code, done.title.as_str()), (JobState::Finished, 3, "Test job"));
        assert_eq!(done.steps, ["A", "B"]);
        let log = fs::read_to_string(job.join("job.log")).unwrap();
        // stdout is the file, not a pipe the store could close
        assert!(log.contains("out\n") && log.contains("err\n") && log.contains(&format!("{}/job.log", job.display())));
        // Its own session: closing the store sends it no hangup
        let sid = log.trim().lines().last().unwrap().to_string();
        let mine = Command::new("ps").args(["-o", "sid=", "-p", &std::process::id().to_string()]).output().unwrap();
        assert!(!sid.is_empty() && sid != String::from_utf8_lossy(&mine.stdout).trim());
        assert!(watcher.read_new(false).contains("out") && watcher.read_new(false).is_empty());
        clear_job(&job);
        assert_eq!(read_job(&job).state, JobState::None);
    }

    #[test]
    fn output_comes_in_whole_lines_while_running() {
        let root = tempfile::tempdir().unwrap();
        let job = root.path().join("job");
        launch_job(&job, &sh("Slow", "printf 'first\\nsec'; sleep 2; echo ond")).unwrap();
        let mut slow = JobWatcher::new(&job);
        sleep(Duration::from_millis(700));
        assert_eq!(read_job(&job).state, JobState::Running);
        assert_eq!(slow.poll(), Poll::Running);
        assert_eq!(slow.read_new(false), "first\n");
        assert_eq!(wait(&slow), Poll::Ended(0));
        assert_eq!(slow.read_new(true), "second\n");
    }

    #[test]
    fn dead_runner_without_exit_code_is_interrupted() {
        let root = tempfile::tempdir().unwrap();
        let job = root.path().join("job");
        fs::create_dir_all(&job).unwrap();
        fs::write(job.join("job.json"), r#"{"title":"Gone","steps":[]}"#).unwrap();
        fs::write(job.join("job.pid"), "999999\n").unwrap();
        assert_eq!(read_job(&job).state, JobState::Interrupted);
        assert_eq!(JobWatcher::new(&job).poll(), Poll::Ended(-1));
    }

    #[test]
    fn last_log_outlives_the_job() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("job.log"), "==> AUR packages\nboom\n").unwrap();
        keep_job_log(dir.path());
        clear_job(dir.path());
        assert!(!dir.path().join("job.log").exists());
        assert_eq!(fs::read_to_string(dir.path().join("last-job.log")).unwrap(), "==> AUR packages\nboom\n");
    }

    #[test]
    fn restart_only_when_own_system_files_were_replaced() {
        assert!(!runs_replaced_files("7f00 r-xp 00000000 00:1b 1  /usr/lib/libQt6Core.so.6.10\n7f01 rw-p 00000000 00:00 0\n7f02 r--s 0 00:01 2  /memfd:x (deleted)\n7f03 r--p 0 00:1b 3  /home/u/old (deleted)\n"));
        assert!(runs_replaced_files("7f00 r-xp 00000000 00:1b 1  /usr/lib/libQt6Core.so.6.10 (deleted)\n"));
        assert!(runs_replaced_files("7f00 r-xp 00000000 00:1b 1  /usr/bin/woof-store (deleted)\n"));
    }

    /// The root helper refuses anything outside its verbs before running a single command.
    #[test]
    fn root_helper_refuses_odd_requests() {
        use std::os::unix::fs::{symlink, PermissionsExt};
        let bin = tempfile::tempdir().unwrap();
        for name in ["pacman", "systemctl", "flatpak"] {
            let path = bin.path().join(name);
            fs::write(&path, format!("#!/bin/sh\necho \"{name} $*\" >> \"$LOG\"\n")).unwrap();
            fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).unwrap();
        }
        let package = bin.path().join("foo-1-1-x86_64.pkg.tar.zst");
        fs::write(&package, "").unwrap();
        let link = bin.path().join("link-1-1-x86_64.pkg.tar.zst");
        symlink(&package, &link).unwrap();
        let log = bin.path().join("log");
        let run = |args: &[&str]| {
            let _ = fs::remove_file(&log);
            let status =
                Command::new(root_helper()).args(args).env("PATH", format!("{}:/usr/bin:/bin", bin.path().display())).env("LOG", &log).output().unwrap().status;
            (status.code().unwrap_or(-1), fs::read_to_string(&log).unwrap_or_default())
        };
        let (package, link) = (package.to_str().unwrap(), link.to_str().unwrap());
        for refused in [
            vec![],
            vec!["sh", "-c", "id"],
            vec!["install", "--noconfirm"],
            vec!["install", "a", "b"],
            vec!["install", "$(id)"],
            vec!["remove"],
            vec!["remove", "ok", "-Rdd"],
            vec!["install-files", "foo-1-1-x86_64.pkg.tar.zst"],
            vec!["install-files", link],
            vec!["install-files", "/etc/passwd"],
            vec!["upgrade", "--overwrite=*"],
            vec!["restart", "sshd"],
            vec!["restart", "a.service;id"],
            vec!["flatpak-files", package],
        ] {
            assert_eq!(run(&refused), (2, String::new()), "{refused:?}");
        }
        assert_eq!(run(&["install", "krita"]), (0, "pacman -S --noconfirm --needed -- krita\n".into()));
        assert_eq!(run(&["install-files", "--needed", package]), (0, format!("pacman -U --noconfirm --needed -- {package}\n")));
    }
}
