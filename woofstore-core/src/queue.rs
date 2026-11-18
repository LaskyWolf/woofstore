//! The order package operations run in. Each lane runs one operation at a time and the lanes run side by side, because
//! pacman locks its database but Flatpak and AppImage imports do not need that lock. The exclusive lane waits for every
//! lane to be idle, and nothing starts while it runs.

use std::collections::{BTreeMap, HashSet};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Lane {
    /// System Update, the store's own update, restarting services: alone.
    Exclusive,
    /// pacman and AUR builds (the build ends with pacman -U).
    Pacman,
    Flatpak,
    AppImage,
}

impl Lane {
    pub const ALL: [Lane; 4] = [Lane::Exclusive, Lane::Pacman, Lane::Flatpak, Lane::AppImage];

    /// The job folder of the lane, below the store's data folder. The exclusive lane keeps the name older versions used,
    /// so a job they started is still found.
    pub fn folder(self) -> &'static str {
        match self {
            Lane::Exclusive => "job",
            Lane::Pacman => "job-pacman",
            Lane::Flatpak => "job-flatpak",
            Lane::AppImage => "job-appimage",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Waiting<T> {
    pub id: u64,
    pub lane: Lane,
    pub job: T,
}

#[derive(Debug, Clone)]
pub struct Queue<T> {
    waiting: Vec<Waiting<T>>,
    running: BTreeMap<Lane, u64>,
    next_id: u64,
}

impl<T> Default for Queue<T> {
    fn default() -> Self {
        Self { waiting: Vec::new(), running: BTreeMap::new(), next_id: 1 }
    }
}

impl<T> Queue<T> {
    /// Adds an operation at the end and returns its id.
    pub fn push(&mut self, lane: Lane, job: T) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        self.waiting.push(Waiting { id, lane, job });
        id
    }

    /// Takes every waiting operation that may start now, oldest first, and marks its lane busy. An operation never
    /// overtakes an earlier one of its lane, and nothing overtakes a waiting exclusive one.
    pub fn start_ready(&mut self) -> Vec<Waiting<T>> {
        let mut started = Vec::new();
        let mut held = HashSet::new();
        let mut i = 0;
        while i < self.waiting.len() {
            let lane = self.waiting[i].lane;
            let free = if lane == Lane::Exclusive {
                self.running.is_empty()
            } else {
                !self.running.contains_key(&Lane::Exclusive) && !self.running.contains_key(&lane) && !held.contains(&lane)
            };
            if !free {
                if lane == Lane::Exclusive {
                    break;
                }
                held.insert(lane);
                i += 1;
                continue;
            }
            let job = self.waiting.remove(i);
            self.running.insert(lane, job.id);
            started.push(job);
        }
        started
    }

    /// The lane's operation has ended; returns its id.
    pub fn finish(&mut self, lane: Lane) -> Option<u64> {
        self.running.remove(&lane)
    }

    /// A lane is busy again with an operation found running after a restart of the store.
    pub fn adopt(&mut self, lane: Lane) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        self.running.insert(lane, id);
        id
    }

    /// Removes an operation that has not started. False when it is not waiting.
    pub fn cancel(&mut self, id: u64) -> bool {
        let before = self.waiting.len();
        self.waiting.retain(|w| w.id != id);
        self.waiting.len() != before
    }

    pub fn waiting(&self) -> &[Waiting<T>] {
        &self.waiting
    }

    pub fn running(&self) -> impl Iterator<Item = (Lane, u64)> + '_ {
        self.running.iter().map(|(lane, id)| (*lane, *id))
    }

    pub fn is_idle(&self) -> bool {
        self.waiting.is_empty() && self.running.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn names(started: Vec<Waiting<&'static str>>) -> Vec<&'static str> {
        started.into_iter().map(|w| w.job).collect()
    }

    #[test]
    fn lanes_run_side_by_side_and_one_at_a_time() {
        let mut queue = Queue::default();
        queue.push(Lane::Pacman, "p1");
        queue.push(Lane::Pacman, "p2");
        queue.push(Lane::Flatpak, "f1");
        assert_eq!(names(queue.start_ready()), ["p1", "f1"]);
        assert_eq!(queue.waiting().len(), 1);
        assert!(queue.start_ready().is_empty());
        queue.finish(Lane::Pacman);
        assert_eq!(names(queue.start_ready()), ["p2"]);
    }

    #[test]
    fn exclusive_waits_for_every_lane_and_nothing_overtakes_it() {
        let mut queue = Queue::default();
        queue.push(Lane::Pacman, "p1");
        queue.push(Lane::Flatpak, "f1");
        queue.start_ready();
        queue.push(Lane::Exclusive, "x");
        queue.push(Lane::AppImage, "a1"); // Its lane is free, but it came after the exclusive one
        assert!(queue.start_ready().is_empty());
        queue.finish(Lane::Pacman);
        assert!(queue.start_ready().is_empty());
        queue.finish(Lane::Flatpak);
        assert_eq!(names(queue.start_ready()), ["x"]);
        assert!(queue.start_ready().is_empty()); // Nothing starts while it runs
        queue.finish(Lane::Exclusive);
        assert_eq!(names(queue.start_ready()), ["a1"]);
        queue.finish(Lane::AppImage);
        assert!(queue.is_idle());
    }

    #[test]
    fn waiting_operations_can_be_cancelled() {
        let mut queue = Queue::default();
        queue.push(Lane::Pacman, "p1");
        let second = queue.push(Lane::Pacman, "p2");
        queue.start_ready();
        assert!(queue.cancel(second) && !queue.cancel(second));
        queue.finish(Lane::Pacman);
        assert!(queue.start_ready().is_empty() && queue.is_idle());
    }

    #[test]
    fn an_adopted_job_keeps_its_lane_busy() {
        let mut queue = Queue::default();
        queue.adopt(Lane::Flatpak);
        queue.push(Lane::Flatpak, "f1");
        assert!(queue.start_ready().is_empty());
        queue.finish(Lane::Flatpak);
        assert_eq!(names(queue.start_ready()), ["f1"]);
    }
}
