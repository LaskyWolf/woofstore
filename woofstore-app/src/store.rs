//! The object the QML pages talk to. Lists go to QML as JSON text: QML parses it into plain arrays, which keeps the
//! bridge small. Slow work runs on a thread and hands its result back to the window's thread.

#[cxx_qt::bridge]
pub mod qobject {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    #[auto_cxx_name]
    extern "RustQt" {
        #[qobject]
        #[qml_element]
        /// JSON array of the operations of this session
        #[qproperty(QString, activity)]
        #[qproperty(bool, busy)]
        /// JSON array of installed apps
        #[qproperty(QString, installed)]
        #[qproperty(bool, installed_loading)]
        /// Problems of the last loads, one per line
        #[qproperty(QString, problems)]
        type Store = super::StoreRust;

        /// Reads the jobs' new output and starts what the queue allows; QML calls it while `busy`.
        #[qinvokable]
        fn tick(self: Pin<&mut Store>);

        /// Takes a waiting operation out of the queue. False once it has started.
        #[qinvokable]
        fn cancel(self: Pin<&mut Store>, id: i32) -> bool;

        #[qinvokable]
        fn clear_ended(self: Pin<&mut Store>);

        /// The whole output of one operation so far.
        #[qinvokable]
        fn job_log(self: &Store, id: i32) -> QString;

        #[qinvokable]
        fn load_installed(self: Pin<&mut Store>);

        /// Queues a package operation. `lane`: "exclusive", "pacman", "flatpak" or "appimage". `spec`: JSON
        /// {"title", "program", "args": [...], "steps": [...] (optional), "cwd" (optional), "stepMarkers": [...] (optional,
        /// one text per step that shows the step was reached)}. Returns the operation's id,
        /// or 0 when it was refused; `notice` then says why.
        #[qinvokable]
        fn queue_job(self: Pin<&mut Store>, lane: &QString, spec: &QString) -> i32;

        /// Once per run: when the running kernel's files are gone (a new kernel was installed), a desktop notification
        /// offers a restart; `reboot_asked` follows when the person picks it, `reboot_needed` when no notification can be shown.
        #[qinvokable]
        fn check_reboot(self: Pin<&mut Store>);

        #[qinvokable]
        fn reboot(self: &Store);

        #[qsignal]
        fn reboot_asked(self: Pin<&mut Store>);

        #[qsignal]
        fn reboot_needed(self: Pin<&mut Store>);

        /// The operation that copies AppImages into the app menu, for `queueJob("appimage", ...)`. `files`: JSON array of paths.
        #[qinvokable]
        fn import_app_images_job(self: &Store, files: &QString) -> QString;

        /// A message for the person: a refused operation and the like.
        #[qsignal]
        fn notice(self: Pin<&mut Store>, text: QString);

        #[qsignal]
        fn job_ended(self: Pin<&mut Store>, id: i32, code: i32);
    }

    impl cxx_qt::Threading for Store {}
}

use core::pin::Pin;
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::QString;
use serde_json::json;
use woofstore_core::jobs::JobSpec;
use woofstore_core::queue::Lane;
use woofstore_system::activity::{lane_name, Activity, Status};
use woofstore_system::{installed, paths, update_check::System};

pub struct StoreRust {
    activity: QString,
    busy: bool,
    installed: QString,
    installed_loading: bool,
    problems: QString,
    jobs: Activity,
    reboot_checked: bool,
}

impl Default for StoreRust {
    fn default() -> Self {
        let jobs = Activity::new(paths::app_data());
        Self {
            activity: QString::from(&activity_json(&jobs)),
            busy: jobs.busy(),
            installed: QString::from("[]"),
            installed_loading: false,
            problems: QString::default(),
            jobs,
            reboot_checked: false,
        }
    }
}

fn activity_json(jobs: &Activity) -> String {
    let records: Vec<_> = jobs
        .records()
        .iter()
        .rev() // Newest first
        .map(|r| {
            let (state, code) = match r.status {
                Status::Waiting => ("waiting", 0),
                Status::Running => ("running", 0),
                Status::Ended(code) => (if code == 0 { "done" } else { "failed" }, code),
                Status::Cancelled => ("cancelled", 0),
            };
            let last_line = r.log.lines().rev().map(str::trim).find(|l| !l.is_empty()).unwrap_or("");
            json!({
                "id": r.id, "title": r.title, "lane": lane_name(r.lane), "state": state, "code": code,
                "step": r.step(), "steps": r.steps, "lastLine": last_line,
            })
        })
        .collect();
    serde_json::Value::from(records).to_string()
}

fn parse_job(lane: &str, spec: &str) -> Result<(Lane, JobSpec), String> {
    let lane = match lane {
        "exclusive" => Lane::Exclusive,
        "pacman" => Lane::Pacman,
        "flatpak" => Lane::Flatpak,
        "appimage" => Lane::AppImage,
        other => return Err(format!("Unknown lane {other}")),
    };
    let value: serde_json::Value = serde_json::from_str(spec).map_err(|e| format!("Invalid operation: {e}"))?;
    let strings = |key: &str| -> Vec<String> { value[key].as_array().into_iter().flatten().filter_map(|v| v.as_str().map(str::to_string)).collect() };
    let text = |key: &str| value[key].as_str().unwrap_or("").to_string();
    let spec = JobSpec {
        title: text("title"),
        program: text("program").into(),
        args: strings("args"),
        steps: strings("steps"),
        cwd: Some(text("cwd")).filter(|c| !c.is_empty()).map(Into::into),
        env: Vec::new(),
    };
    if spec.title.is_empty() || spec.program.as_os_str().is_empty() {
        return Err("Invalid operation: title and program are needed".into());
    }
    Ok((lane, spec))
}

impl qobject::Store {
    fn publish(mut self: Pin<&mut Self>) {
        let text = QString::from(&activity_json(&self.rust().jobs));
        if *self.activity() != text {
            self.as_mut().set_activity(text);
        }
        let busy = self.rust().jobs.busy();
        self.set_busy(busy);
    }

    fn tick(mut self: Pin<&mut Self>) {
        let ended = self.as_mut().rust_mut().jobs.tick();
        self.as_mut().publish();
        for id in ended {
            let code = match self.rust().jobs.record(id).map(|r| r.status) {
                Some(Status::Ended(code)) => code,
                _ => -1,
            };
            self.as_mut().job_ended(id as i32, code);
        }
    }

    fn cancel(mut self: Pin<&mut Self>, id: i32) -> bool {
        let cancelled = self.as_mut().rust_mut().jobs.cancel(id as u64);
        self.publish();
        cancelled
    }

    fn clear_ended(mut self: Pin<&mut Self>) {
        self.as_mut().rust_mut().jobs.clear_ended();
        self.publish();
    }

    fn queue_job(mut self: Pin<&mut Self>, lane: &QString, spec: &QString) -> i32 {
        let markers = serde_json::from_str::<serde_json::Value>(&spec.to_string())
            .ok()
            .and_then(|v| serde_json::from_value::<Vec<String>>(v["stepMarkers"].clone()).ok());
        let result = parse_job(&lane.to_string(), &spec.to_string()).and_then(|(lane, spec)| self.as_mut().rust_mut().jobs.push(lane, spec));
        if let (Ok(id), Some(markers)) = (&result, markers) {
            self.as_mut().rust_mut().jobs.set_markers(*id, markers);
        }
        self.as_mut().publish();
        match result {
            Ok(id) => id as i32,
            Err(message) => {
                self.notice(QString::from(&message));
                0
            }
        }
    }

    fn check_reboot(mut self: Pin<&mut Self>) {
        let release = std::fs::read_to_string("/proc/sys/kernel/osrelease").unwrap_or_default();
        let modules = std::path::Path::new("/usr/lib/modules");
        if self.rust().reboot_checked || !modules.is_dir() || modules.join(release.trim()).join("vmlinuz").exists() {
            return;
        }
        self.as_mut().rust_mut().reboot_checked = true;
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            // --wait keeps the notification's button working and prints the action picked
            let picked = std::process::Command::new("notify-send")
                .args(["--app-name=WoofStore", "--icon=system-reboot", "--hint=string:desktop-entry:io.github.laskywolf.WoofStore"])
                .args(["--action=restart=Restart now", "--wait", "Restart to finish the update", "A new kernel was installed. Restart when you are ready."])
                .output();
            let _ = thread.queue(move |store| match picked {
                Ok(out) if out.status.success() => {
                    if String::from_utf8_lossy(&out.stdout).trim() == "restart" {
                        store.reboot_asked();
                    }
                }
                _ => store.reboot_needed(),
            });
        });
    }

    fn reboot(&self) {
        let _ = std::process::Command::new("systemctl").arg("reboot").spawn();
    }

    fn import_app_images_job(&self, files: &QString) -> QString {
        let files: Vec<String> = serde_json::from_str(&files.to_string()).unwrap_or_default();
        let program = std::env::current_exe().map(|p| p.to_string_lossy().trim_end_matches(" (deleted)").to_string()).unwrap_or_else(|_| "woof-store".into());
        let name = |f: &String| std::path::Path::new(f).file_stem().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        let title = if files.len() == 1 { format!("Install {}", name(&files[0])) } else { format!("Install {} files", files.len()) };
        let args: Vec<&str> = files.iter().flat_map(|f| ["--import-appimage", f.as_str()]).collect();
        QString::from(
            &json!({
                "title": title, "program": program, "args": args,
                "steps": ["Prepare", "Copy", "Add to menu"], "stepMarkers": ["", "==> Copying", "==> Adding to the app menu"],
            })
            .to_string(),
        )
    }

    fn job_log(&self, id: i32) -> QString {
        QString::from(self.rust().jobs.record(id as u64).map(|r| r.log.as_str()).unwrap_or(""))
    }

    fn load_installed(mut self: Pin<&mut Self>) {
        if self.rust().installed_loading {
            return;
        }
        self.as_mut().set_installed_loading(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let found = installed::load(&System, woofstore_system::appstream::Language::from_environment().code());
            let apps: Vec<_> = found
                .apps
                .iter()
                .map(|a| {
                    json!({
                        "id": a.id, "name": a.name, "summary": a.summary, "version": a.version, "source": a.source,
                        "icon": a.icon, "launcher": a.launcher, "remote": a.remote, "scope": a.scope, "path": a.path,
                    })
                })
                .collect();
            let text = serde_json::Value::from(apps).to_string();
            let problems = found.problems.join("\n");
            let _ = thread.queue(move |mut store| {
                store.as_mut().set_installed(QString::from(&text));
                store.as_mut().set_problems(QString::from(&problems));
                store.set_installed_loading(false);
            });
        });
    }
}
