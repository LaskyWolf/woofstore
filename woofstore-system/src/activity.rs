//! Every package operation of this session: waiting in the queue, running in its lane, or ended. The interface calls
//! [`Activity::tick`] a few times a second; it reads the jobs' output and starts what the queue allows.

use std::collections::HashMap;
use std::path::PathBuf;
use woofstore_core::jobs::{clear_job, keep_job_log, launch_job, read_job, JobSpec, JobState, JobWatcher, Poll};
use woofstore_core::queue::{Lane, Queue};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Waiting,
    Running,
    /// Exit code; -1 when the job vanished or could not start.
    Ended(i32),
    Cancelled,
}

#[derive(Debug, Clone)]
pub struct Record {
    pub id: u64,
    pub title: String,
    pub lane: Lane,
    pub steps: Vec<String>,
    pub status: Status,
    pub log: String,
    /// Text whose presence in the output means the job reached that step; empty entries never match.
    pub markers: Vec<String>,
}

impl Record {
    /// The step the job is in: the last marker its output contains; without markers the last "==> Step" line the scripts
    /// print, then the words pacman and flatpak print.
    pub fn step(&self) -> usize {
        if !self.markers.is_empty() {
            return self.markers.iter().rposition(|m| !m.is_empty() && self.log.contains(m.as_str())).unwrap_or(0);
        }
        if let Some(step) = self.steps.iter().rposition(|s| self.log.contains(&format!("==> {s}"))) {
            return step;
        }
        let lower = self.log.to_lowercase();
        if lower.contains("install") || lower.contains("remov") {
            self.steps.len().saturating_sub(1)
        } else if ["download", "fetch", "resolving"].iter().any(|w| lower.contains(w)) {
            1.min(self.steps.len().saturating_sub(1))
        } else {
            0
        }
    }
}

pub struct Activity {
    data: PathBuf,
    queue: Queue<JobSpec>,
    records: Vec<Record>,
    watchers: HashMap<Lane, (u64, JobWatcher)>,
}

impl Activity {
    /// Picks up jobs an earlier run of the store started: running ones are watched again, ended ones are reported.
    pub fn new(data: impl Into<PathBuf>) -> Self {
        let mut activity = Self { data: data.into(), queue: Queue::default(), records: Vec::new(), watchers: HashMap::new() };
        for lane in Lane::ALL {
            let dir = activity.dir(lane);
            let info = read_job(&dir);
            let status = match info.state {
                JobState::None => continue,
                JobState::Running => Status::Running,
                JobState::Finished => Status::Ended(info.exit_code),
                JobState::Interrupted => Status::Ended(-1),
            };
            let id = activity.queue.adopt(lane);
            let mut watcher = JobWatcher::new(&dir);
            let log = watcher.read_new(status != Status::Running);
            activity.records.push(Record { id, title: info.title, lane, steps: info.steps, status, log, markers: Vec::new() });
            if status == Status::Running {
                activity.watchers.insert(lane, (id, watcher));
            } else {
                activity.queue.finish(lane);
                keep_job_log(&dir);
                clear_job(&dir);
            }
        }
        activity
    }

    pub fn dir(&self, lane: Lane) -> PathBuf {
        self.data.join(lane.folder())
    }

    pub fn records(&self) -> &[Record] {
        &self.records
    }

    pub fn record(&self, id: u64) -> Option<&Record> {
        self.records.iter().find(|r| r.id == id)
    }

    pub fn busy(&self) -> bool {
        !self.queue.is_idle()
    }

    pub fn lane_busy(&self, lane: Lane) -> bool {
        self.queue.running().any(|(l, _)| l == lane) || self.queue.waiting().iter().any(|w| w.lane == lane)
    }

    /// Queues the job and starts it at once when its lane is free. Refused when the program is missing or an operation with
    /// the same title is already waiting or running.
    pub fn push(&mut self, lane: Lane, mut spec: JobSpec) -> Result<u64, String> {
        let program = spec.program.to_string_lossy().into_owned();
        let found = if program.contains('/') { spec.program.is_file() } else { find_executable(&program).is_some() };
        if !found {
            return Err(format!("Install {program} and try again."));
        }
        if self.records.iter().any(|r| r.title == spec.title && matches!(r.status, Status::Waiting | Status::Running)) {
            return Err(format!("{} is already running or waiting", spec.title));
        }
        if spec.steps.is_empty() {
            spec.steps = if spec.title.starts_with("Remove") {
                vec!["Prepare".into(), "Remove".into()]
            } else {
                vec!["Prepare".into(), "Download".into(), "Install".into()]
            };
        }
        // sudo has no terminal here, so it asks for the password through this helper
        let askpass = self.data.join("askpass.sh");
        if write_askpass(&askpass).is_ok() {
            spec.env.push(("SUDO_ASKPASS".into(), askpass.to_string_lossy().into_owned()));
        }
        spec.env.push(("LC_ALL".into(), "C.UTF-8".into()));
        let record =
            Record { id: 0, title: spec.title.clone(), lane, steps: spec.steps.clone(), status: Status::Waiting, log: String::new(), markers: Vec::new() };
        let id = self.queue.push(lane, spec);
        self.records.push(Record { id, ..record });
        self.pump();
        Ok(id)
    }

    /// Sets the step markers of an operation (see [`Record::markers`]).
    pub fn set_markers(&mut self, id: u64, markers: Vec<String>) {
        if let Some(record) = self.records.iter_mut().find(|r| r.id == id) {
            record.markers = markers;
        }
    }

    /// Takes a waiting job out of the queue. False once it has started.
    pub fn cancel(&mut self, id: u64) -> bool {
        let cancelled = self.queue.cancel(id);
        if cancelled {
            self.set_status(id, Status::Cancelled);
            self.pump(); // A cancelled exclusive job no longer holds the others back
        }
        cancelled
    }

    /// Forgets ended and cancelled jobs.
    pub fn clear_ended(&mut self) {
        self.records.retain(|r| matches!(r.status, Status::Waiting | Status::Running));
    }

    /// Reads new output and notices ended jobs. Returns the ids of jobs that ended in this tick.
    pub fn tick(&mut self) -> Vec<u64> {
        let mut ended = Vec::new();
        let lanes: Vec<Lane> = self.watchers.keys().copied().collect();
        for lane in lanes {
            let (id, watcher) = self.watchers.get_mut(&lane).expect("listed above");
            let id = *id;
            let poll = watcher.poll();
            let text = watcher.read_new(poll != Poll::Running);
            if let Some(record) = self.records.iter_mut().find(|r| r.id == id) {
                record.log.push_str(&text);
            }
            if let Poll::Ended(code) = poll {
                self.watchers.remove(&lane);
                self.set_status(id, Status::Ended(code));
                let dir = self.dir(lane);
                keep_job_log(&dir);
                clear_job(&dir);
                self.queue.finish(lane);
                ended.push(id);
            }
        }
        if !ended.is_empty() {
            self.pump();
        }
        ended
    }

    fn set_status(&mut self, id: u64, status: Status) {
        if let Some(record) = self.records.iter_mut().find(|r| r.id == id) {
            record.status = status;
        }
    }

    fn pump(&mut self) {
        loop {
            let started = self.queue.start_ready();
            if started.is_empty() {
                return;
            }
            for job in started {
                let dir = self.dir(job.lane);
                match launch_job(&dir, &job.job) {
                    Ok(()) => {
                        self.set_status(job.id, Status::Running);
                        self.watchers.insert(job.lane, (job.id, JobWatcher::new(&dir)));
                    }
                    Err(error) => {
                        if let Some(record) = self.records.iter_mut().find(|r| r.id == job.id) {
                            record.log = error;
                            record.status = Status::Ended(-1);
                        }
                        self.queue.finish(job.lane); // Free the lane and let the loop start the next one
                    }
                }
            }
        }
    }
}

fn write_askpass(path: &std::path::Path) -> std::io::Result<()> {
    use std::os::unix::fs::PermissionsExt;
    std::fs::create_dir_all(path.parent().expect("inside the data folder"))?;
    std::fs::write(
        path,
        "#!/bin/sh\nif command -v kdialog >/dev/null; then exec kdialog --title WoofStore --password \"${1:-Password}\"; fi\nexec zenity --password --title=WoofStore\n",
    )?;
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700))
}

pub fn find_executable(name: &str) -> Option<PathBuf> {
    std::env::split_paths(&std::env::var_os("PATH")?).map(|dir| dir.join(name)).find(|path| path.is_file())
}

pub fn lane_name(lane: Lane) -> &'static str {
    match lane {
        Lane::Exclusive => "System",
        Lane::Pacman => "Packages",
        Lane::Flatpak => "Flatpak",
        Lane::AppImage => "AppImage",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{Duration, Instant};

    fn sh(title: &str, script: &str) -> JobSpec {
        JobSpec {
            title: title.into(),
            steps: vec!["One".into(), "Two".into()],
            program: "sh".into(),
            args: vec!["-c".into(), script.into()],
            ..Default::default()
        }
    }

    fn run_until_idle(activity: &mut Activity) {
        let start = Instant::now();
        while activity.busy() && start.elapsed() < Duration::from_secs(20) {
            activity.tick();
            std::thread::sleep(Duration::from_millis(50));
        }
    }

    #[test]
    fn lanes_run_side_by_side_and_exclusive_waits() {
        let data = tempfile::tempdir().unwrap();
        let mut activity = Activity::new(data.path());
        let pacman = activity.push(Lane::Pacman, sh("Install a", "echo '==> Two'; sleep 0.5")).unwrap();
        assert!(activity.push(Lane::Pacman, sh("Install a", "true")).unwrap_err().contains("already running"));
        assert!(activity.push(Lane::Pacman, JobSpec { program: "no-such-program-here".into(), ..sh("x", "") }).unwrap_err().contains("Install no-such"));
        let flatpak = activity.push(Lane::Flatpak, sh("Install b", "exit 3")).unwrap();
        let system = activity.push(Lane::Exclusive, sh("System Update", "echo done")).unwrap();
        let later = activity.push(Lane::AppImage, sh("Import c", "true")).unwrap();
        let status = |a: &Activity, id| a.record(id).unwrap().status;
        assert_eq!((status(&activity, pacman), status(&activity, flatpak)), (Status::Running, Status::Running));
        assert_eq!((status(&activity, system), status(&activity, later)), (Status::Waiting, Status::Waiting)); // Nothing overtakes it
        assert!(activity.cancel(later));
        assert!(!activity.cancel(pacman));
        run_until_idle(&mut activity);
        assert_eq!(status(&activity, pacman), Status::Ended(0));
        assert_eq!(activity.record(pacman).unwrap().step(), 1);
        assert_eq!(status(&activity, flatpak), Status::Ended(3));
        assert_eq!(status(&activity, system), Status::Ended(0));
        assert_eq!(activity.record(system).unwrap().log, "done\n");
        assert_eq!(status(&activity, later), Status::Cancelled);
        assert!(data.path().join("job/last-job.log").exists() && data.path().join("askpass.sh").exists());
        let mut record = activity.record(flatpak).unwrap().clone();
        record.log = "Fetching metadata\nInstalling app\n".into();
        assert_eq!(record.step(), 1); // "Install a" has steps One, Two: the last one
        record.markers = vec![String::new(), "Making package".into()];
        assert_eq!(record.step(), 0);
        activity.clear_ended();
        assert!(activity.records().is_empty());
    }

    #[test]
    fn a_job_from_an_earlier_run_is_picked_up() {
        let data = tempfile::tempdir().unwrap();
        let mut first = Activity::new(data.path());
        first.push(Lane::Pacman, sh("Long", "sleep 1; echo finished")).unwrap();
        drop(first); // The store quits; the job keeps running in its own session
        let mut second = Activity::new(data.path());
        assert_eq!(second.records()[0].status, Status::Running);
        assert_eq!(second.records()[0].title, "Long");
        run_until_idle(&mut second);
        assert_eq!(second.records()[0].status, Status::Ended(0));
        assert!(second.records()[0].log.contains("finished"));
    }
}
