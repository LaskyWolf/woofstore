//! Runs a program and collects its output, with a time limit. The output is always in the C locale, because the
//! store parses it.

use std::io::Read;
use std::process::{Command, Stdio};
use std::thread;
use std::time::Duration;
use wait_timeout::ChildExt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Output {
    /// The exit code; -1 when the program could not start, was killed or timed out.
    pub code: i32,
    pub stdout: String,
    /// The program's error output, or why it did not run.
    pub stderr: String,
}

impl Output {
    pub fn ok(&self) -> bool {
        self.code == 0
    }
}

pub const DEFAULT_TIMEOUT: Duration = Duration::from_secs(30);

pub fn run(program: &str, args: &[&str], timeout: Duration) -> Output {
    let child = Command::new(program).args(args).env("LC_ALL", "C.UTF-8").stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn();
    let mut child = match child {
        Ok(child) => child,
        Err(e) => return Output { code: -1, stdout: String::new(), stderr: format!("Cannot start {program}: {e}") },
    };
    // Read both pipes while the program runs, so a large output cannot fill a pipe and stall it.
    let mut stdout = child.stdout.take().expect("piped");
    let mut stderr = child.stderr.take().expect("piped");
    let out = thread::spawn(move || {
        let mut bytes = Vec::new();
        let _ = stdout.read_to_end(&mut bytes);
        bytes
    });
    let err = thread::spawn(move || {
        let mut bytes = Vec::new();
        let _ = stderr.read_to_end(&mut bytes);
        bytes
    });
    let (code, timed_out) = match child.wait_timeout(timeout) {
        Ok(Some(status)) => (status.code().unwrap_or(-1), false),
        _ => {
            let _ = child.kill();
            let _ = child.wait();
            (-1, true)
        }
    };
    let stdout = String::from_utf8_lossy(&out.join().unwrap_or_default()).into_owned();
    let mut stderr = String::from_utf8_lossy(&err.join().unwrap_or_default()).into_owned();
    if timed_out {
        stderr = "The request timed out. Check your connection and retry.".into();
    }
    Output { code, stdout, stderr }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn output_code_and_locale() {
        let result = run("sh", &["-c", "echo \"$LC_ALL\"; echo oops >&2; exit 4"], DEFAULT_TIMEOUT);
        assert_eq!((result.code, result.stdout.as_str(), result.stderr.as_str()), (4, "C.UTF-8\n", "oops\n"));
    }

    #[test]
    fn missing_program_and_timeout() {
        let missing = run("woofstore-no-such-program", &[], DEFAULT_TIMEOUT);
        assert!(missing.code == -1 && missing.stderr.contains("Cannot start"));
        let slow = run("sleep", &["5"], Duration::from_millis(200));
        assert!(slow.code == -1 && slow.stderr.contains("timed out"));
    }

    #[test]
    fn large_output_does_not_stall() {
        let big = run("sh", &["-c", "head -c 1000000 /dev/zero | tr '\\0' x; head -c 200000 /dev/zero | tr '\\0' y >&2"], DEFAULT_TIMEOUT);
        assert!(big.ok() && big.stdout.len() == 1_000_000 && big.stderr.len() == 200_000);
    }
}
