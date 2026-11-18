//! The step between "Install" on an AUR package and the build. The package and every AUR package it needs are downloaded
//! and shown one at a time; nothing is built until each is approved, and the build uses exactly the files that were read.

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
        /// Downloading or reading build files
        #[qproperty(bool, busy)]
        /// JSON {who, facts, flags: [...], text, needs: [...]} of the package to approve; empty when nothing waits
        #[qproperty(QString, review)]
        type AurReview = super::AurReviewRust;

        /// Starts the review of AUR package `name`. `title` names the app in the operation's title.
        #[qinvokable]
        fn start(self: Pin<&mut AurReview>, name: &QString, title: &QString, reinstall: bool);

        /// The person read the shown recipe: review the next package, or hand over the build.
        #[qinvokable]
        fn approve(self: Pin<&mut AurReview>);

        #[qinvokable]
        fn cancel(self: Pin<&mut AurReview>);

        #[qsignal]
        fn failed(self: Pin<&mut AurReview>, title: QString, message: QString);

        /// The operation JSON for `store.queueJob("pacman", spec)`.
        #[qsignal]
        fn build_ready(self: Pin<&mut AurReview>, spec: QString);
    }

    impl cxx_qt::Threading for AurReview {}
}

use core::pin::Pin;
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::QString;
use serde_json::json;
use std::collections::{HashMap, VecDeque};
use std::path::Path;
use std::time::Duration;
use woofstore_core::aur::{aur_build_order, aur_build_script, chroot_pacman_conf, dependency_name, srcinfo_dependencies};
use woofstore_core::review::{load_reviewed, review_flags, review_unchanged, save_reviewed, AurInfo, ReviewSnapshot};
use woofstore_system::activity::find_executable;
use woofstore_system::aur_remote::{aur_info, missing_from_repos, prepare_aur, PreparedReview};
use woofstore_system::{command, paths, update_check::System};

/// More than this many AUR packages for one install is a sign to build by hand.
const MOST_PACKAGES: usize = 8;

struct Current {
    name: String,
    base: String,
    prepared: PreparedReview,
    aur_names: Vec<String>,
}

struct Plan {
    root: String,
    title: String,
    reinstall: bool,
    queue: VecDeque<String>,
    seen: Vec<String>,
    needs: HashMap<String, Vec<String>>,
    files: HashMap<String, ReviewSnapshot>,
    current: Option<Current>,
}

#[derive(Default)]
pub struct AurReviewRust {
    busy: bool,
    review: QString,
    plan: Option<Plan>,
    /// Build folders of operations already handed over; they must outlive the build.
    building: Vec<ReviewSnapshot>,
}

fn reviewed_dir() -> std::path::PathBuf {
    paths::app_data().join("reviewed")
}

fn date(seconds: Option<i64>) -> Option<String> {
    seconds.map(woofstore_system::http::iso_date)
}

fn facts(info: &AurInfo) -> String {
    let mut lines = vec![format!("Package: {}{}", info.name, if info.base != info.name { format!(" (built from {})", info.base) } else { String::new() })];
    lines.push(format!("Maintainer: {}", if info.maintainer.is_empty() { "none (orphaned)" } else { &info.maintainer }));
    lines.push(format!("Votes: {} · popularity {:.2}", info.votes, info.popularity));
    if let Some(d) = date(info.first_submitted) {
        lines.push(format!("First submitted: {d}"));
    }
    if let Some(d) = date(info.last_modified) {
        lines.push(format!("Last updated: {d}"));
    }
    if let Some(d) = date(info.out_of_date) {
        lines.push(format!("Flagged out of date: {d}"));
    }
    lines.join("\n")
}

/// `diff -u` of two texts; empty when they are the same or diff is missing.
fn unified_diff(old: &str, new: &str) -> String {
    let Ok(dir) = tempfile::tempdir() else { return String::new() };
    let (a, b) = (dir.path().join("approved"), dir.path().join("current"));
    if std::fs::write(&a, old).is_err() || std::fs::write(&b, new).is_err() {
        return String::new();
    }
    command::run("diff", &["-u", &a.to_string_lossy(), &b.to_string_lossy()], Duration::from_secs(5)).stdout
}

/// Downloads and reads one package's build files: everything the review dialog shows. Errors are (title, message).
fn prepare(name: &str, who: &str, root: &str) -> Result<(Current, serde_json::Value), (String, String)> {
    let info = aur_info(&System, name).map_err(|e| (format!("Cannot install {root}"), format!("{who}: {e}")))?;
    let prepared = prepare_aur(&System, &paths::cache(), &info.base).map_err(|e| (format!("Cannot review {name}"), e))?;
    let srcinfo = prepared.snapshot.path().and_then(|p| std::fs::read_to_string(p.join(".SRCINFO")).ok()).unwrap_or_default();
    let dependencies = srcinfo_dependencies(&srcinfo);
    let refs: Vec<&str> = dependencies.iter().map(String::as_str).collect();
    let mut aur_names: Vec<String> = Vec::new();
    for missing in missing_from_repos(&System, &refs) {
        let dependency = dependency_name(&missing).to_string();
        if !aur_names.contains(&dependency) {
            aur_names.push(dependency);
        }
    }
    let text = match load_reviewed(&reviewed_dir(), &info.base) {
        Some((_, previous)) if !previous.is_empty() => match unified_diff(&previous, &prepared.recipe) {
            changes if changes.is_empty() => format!("No changes since you approved this recipe last time.\n\n{}", prepared.recipe),
            changes => format!("Changes since you approved this recipe last time:\n\n{changes}\n\n══════ Full recipe ══════\n{}", prepared.recipe),
        },
        _ => prepared.recipe.clone(),
    };
    let flags = review_flags(&prepared.recipe, &info.url);
    let shown = json!({ "who": who, "facts": facts(&info), "flags": flags, "text": text, "needs": aur_names });
    Ok((Current { name: name.to_string(), base: info.base, prepared, aur_names }, shown))
}

impl qobject::AurReview {
    fn start(mut self: Pin<&mut Self>, name: &QString, title: &QString, reinstall: bool) {
        if self.rust().busy || self.rust().plan.is_some() {
            return;
        }
        for tool in ["mkarchroot", "makechrootpkg", "arch-nspawn"] {
            if find_executable(tool).is_none() {
                self.failed(
                    QString::from("Clean chroot tools missing"),
                    QString::from("AUR packages are built in a clean chroot with the tools of the devtools package.\n\nInstall it, then try again:\n\nsudo pacman -S --needed devtools"),
                );
                return;
            }
        }
        let root = name.to_string();
        self.as_mut().rust_mut().plan = Some(Plan {
            root: root.clone(),
            title: title.to_string(),
            reinstall,
            queue: VecDeque::from([root.clone()]),
            seen: vec![root],
            needs: HashMap::new(),
            files: HashMap::new(),
            current: None,
        });
        self.next();
    }

    /// Reviews the next queued package, or hands over the build when none is left.
    fn next(mut self: Pin<&mut Self>) {
        let step = {
            let mut rust = self.as_mut().rust_mut();
            rust.plan.as_mut().map(|plan| {
                plan.queue.pop_front().map(|name| {
                    let who = if name == plan.root { name.clone() } else { format!("{name} (needed by {})", plan.root) };
                    (name, who, plan.root.clone())
                })
            })
        };
        let Some(step) = step else { return };
        let Some((name, who, root)) = step else {
            self.build();
            return;
        };
        self.as_mut().set_busy(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let result = prepare(&name, &who, &root);
            let _ = thread.queue(move |mut review| {
                review.as_mut().set_busy(false);
                if review.rust().plan.is_none() {
                    return; // Cancelled meanwhile
                }
                match result {
                    Ok((current, shown)) => {
                        review.as_mut().rust_mut().plan.as_mut().expect("checked").current = Some(current);
                        review.set_review(QString::from(&shown.to_string()));
                    }
                    Err((title, message)) => {
                        review.as_mut().rust_mut().plan = None;
                        review.failed(QString::from(&title), QString::from(&message));
                    }
                }
            });
        });
    }

    fn approve(mut self: Pin<&mut Self>) {
        let Some(current) = self.as_mut().rust_mut().plan.as_mut().and_then(|p| p.current.take()) else { return };
        self.as_mut().set_review(QString::default());
        if let Err(message) = review_unchanged(&current.prepared.snapshot) {
            self.as_mut().rust_mut().plan = None;
            self.failed(QString::from("Build files changed"), QString::from(&message));
            return;
        }
        save_reviewed(&reviewed_dir(), &current.base, &current.prepared.commit, &current.prepared.recipe);
        let too_many = {
            let mut rust = self.as_mut().rust_mut();
            let plan = rust.plan.as_mut().expect("checked above");
            for need in &current.aur_names {
                if !plan.seen.contains(need) {
                    plan.seen.push(need.clone());
                    plan.queue.push_back(need.clone());
                }
            }
            plan.needs.insert(current.name.clone(), current.aur_names);
            plan.files.insert(current.name, current.prepared.snapshot);
            (plan.seen.len() > MOST_PACKAGES).then(|| plan.root.clone())
        };
        if let Some(root) = too_many {
            self.as_mut().rust_mut().plan = None;
            self.failed(
                QString::from("Too many AUR dependencies"),
                QString::from(&format!("{root} needs more than {MOST_PACKAGES} AUR packages. Build it by hand.")),
            );
            return;
        }
        self.next();
    }

    fn cancel(mut self: Pin<&mut Self>) {
        self.as_mut().rust_mut().plan = None;
        self.set_review(QString::default());
    }

    fn build(mut self: Pin<&mut Self>) {
        let Some(plan) = self.as_mut().rust_mut().plan.take() else { return };
        match build_spec(&plan) {
            Ok(spec) => {
                self.as_mut().rust_mut().building.extend(plan.files.into_values());
                self.build_ready(QString::from(&spec.to_string()));
            }
            Err((title, message)) => self.failed(QString::from(&title), QString::from(&message)),
        }
    }
}

fn build_spec(plan: &Plan) -> Result<serde_json::Value, (String, String)> {
    let mut items = Vec::new();
    for name in aur_build_order(&plan.root, &plan.needs) {
        let snapshot = plan.files.get(&name).cloned().unwrap_or_default();
        review_unchanged(&snapshot).map_err(|e| ("Build files changed".to_string(), format!("{name}: {e}")))?;
        items.push(format!("{name}\t{}", snapshot.path().expect("reviewed").display()));
    }
    // The chroot copies your pacman.conf, so multilib and CachyOS repositories resolve; only the exact built packages are installed, as root.
    let chroot = paths::cache().join("chroot");
    let read = |path: &str| std::fs::read_to_string(path).unwrap_or_default();
    let conf = chroot_pacman_conf(&read("/etc/pacman.conf"), read);
    woofstore_system::http::save(&chroot.join("pacman.conf"), conf.as_bytes())
        .map_err(|e| ("Cannot prepare the build".to_string(), format!("Cannot write {}/pacman.conf: {e}", chroot.display())))?;
    let root_dir = plan.files.get(&plan.root).and_then(ReviewSnapshot::path).unwrap_or_else(|| Path::new("/").to_path_buf());
    let mut args = vec!["-c".to_string(), aur_build_script().to_string(), "woof-store-aur".into(), plan.root.clone()];
    args.push(if plan.reinstall { String::new() } else { "--needed".into() });
    args.push(chroot.to_string_lossy().into_owned());
    args.extend(items);
    Ok(json!({
        "title": format!("Install {} from the AUR", plan.title),
        "program": "bash",
        "args": args,
        "cwd": root_dir,
        "steps": ["Prepare", "Download", "Build", "Install"],
        "stepMarkers": ["", "Retrieving sources", "Making package", "Installing package"],
    }))
}
