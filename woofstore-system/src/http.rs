//! Web requests. Every request names the store, follows redirects and has a time limit. Answers that change slowly
//! (ratings, rankings) are kept for a day, and the last saved copy is used when the computer is offline.

use sha1::{Digest, Sha1};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

const USER_AGENT: &str = "WoofStore";
const ONE_DAY: Duration = Duration::from_secs(24 * 3600);

fn agent(timeout: Duration) -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(timeout).user_agent(USER_AGENT).redirects(5).build()
}

/// The body of `url` (at most `first_bytes` of it when that is not 0), or the reason it could not be read.
pub fn get(url: &str, first_bytes: u64) -> Result<Vec<u8>, String> {
    let response = agent(Duration::from_secs(20))
        .get(url)
        .set("Accept", "application/vnd.github+json, application/xml, text/html, */*")
        .call()
        .map_err(|e| why_not(&e))?;
    let mut body = Vec::new();
    let reader = response.into_reader();
    let result = if first_bytes > 0 { reader.take(first_bytes).read_to_end(&mut body) } else { reader.take(256 << 20).read_to_end(&mut body) };
    result.map_err(|e| e.to_string())?;
    Ok(body)
}

/// The reason a request failed. GitHub allows 60 API requests an hour from one address without an account; when they
/// are used up it answers 403 with `x-ratelimit-remaining: 0`, and the message says when it lets the store ask again.
fn why_not(error: &ureq::Error) -> String {
    if let ureq::Error::Status(403 | 429, response) = error {
        if response.header("x-ratelimit-remaining") == Some("0") {
            let reset = response.header("x-ratelimit-reset").and_then(|s| s.parse::<u64>().ok()).unwrap_or(0);
            let now = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
            return rate_limit_message(reset.saturating_sub(now));
        }
    }
    error.to_string()
}

fn rate_limit_message(seconds_left: u64) -> String {
    let minutes = seconds_left.div_ceil(60).max(1);
    format!("GitHub's limit of 60 checks an hour is used up; it allows new checks in {minutes} minute{}.", if minutes == 1 { "" } else { "s" })
}

/// A GitHub API answer (release lists). It is reused for an hour, so starting the store often does not use up GitHub's
/// limit; when GitHub cannot answer (offline, or the limit is used up) the last saved answer stands in.
pub fn github_api(cache_dir: &Path, url: &str) -> Result<Vec<u8>, String> {
    let path = cache_path(cache_dir, url);
    if fresh(&path, Duration::from_secs(3600)) {
        if let Ok(data) = fs::read(&path) {
            return Ok(data);
        }
    }
    match get(url, 0) {
        Ok(body) => {
            let _ = save(&path, &body);
            Ok(body)
        }
        Err(error) => fs::read(&path).map_err(|_| error),
    }
}

fn fresh(path: &Path, age: Duration) -> bool {
    fs::metadata(path).and_then(|m| m.modified()).map(|time| SystemTime::now().duration_since(time).unwrap_or_default() < age).unwrap_or(false)
}

/// Writes through a temporary file, so a crash never leaves half a file behind.
pub fn save(path: &Path, data: &[u8]) -> std::io::Result<()> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let temporary = path.with_extension("part");
    fs::write(&temporary, data)?;
    fs::rename(temporary, path)
}

/// A JSON answer kept for a day in `cache_dir/http`. When the request fails or the answer is not a JSON object, the
/// last saved copy is returned; empty when there is none.
pub fn cached_json(cache_dir: &Path, url: &str) -> Vec<u8> {
    let path = cache_path(cache_dir, url);
    if fresh(&path, ONE_DAY) {
        if let Ok(data) = fs::read(&path) {
            return data;
        }
    }
    let answer = agent(Duration::from_secs(15)).get(url).call().ok().and_then(|response| {
        let mut body = Vec::new();
        response.into_reader().take(64 << 20).read_to_end(&mut body).ok()?;
        serde_json::from_slice::<serde_json::Value>(&body).ok().filter(serde_json::Value::is_object).map(|_| body)
    });
    match answer {
        Some(body) => {
            let _ = save(&path, &body);
            body
        }
        None => fs::read(&path).unwrap_or_default(),
    }
}

pub fn cache_path(cache_dir: &Path, url: &str) -> PathBuf {
    cache_dir.join("http").join(format!("{}.json", hex::encode(Sha1::digest(url.as_bytes()))))
}

/// Forgets every saved web answer, so the next requests ask again.
pub fn forget(cache_dir: &Path) {
    let _ = fs::remove_dir_all(cache_dir.join("http"));
}

/// The result of a download that only transfers when the server has something newer than `since`.
#[derive(Debug)]
pub enum Conditional {
    Changed { body: Vec<u8>, modified: Option<SystemTime> },
    Unchanged,
    Failed(String),
}

/// GET with If-Modified-Since: large lists (the AUR dump) download only when they changed.
pub fn get_if_modified(url: &str, since: Option<SystemTime>, timeout: Duration) -> Conditional {
    let mut request = agent(timeout).get(url);
    if let Some(time) = since {
        request = request.set("If-Modified-Since", &httpdate(time));
    }
    match request.call() {
        Ok(response) if response.status() == 304 => Conditional::Unchanged,
        Ok(response) => {
            let modified = response.header("Last-Modified").and_then(parse_httpdate);
            let mut body = Vec::new();
            match response.into_reader().take(512 << 20).read_to_end(&mut body) {
                Ok(_) => Conditional::Changed { body, modified },
                Err(e) => Conditional::Failed(e.to_string()),
            }
        }
        Err(ureq::Error::Status(304, _)) => Conditional::Unchanged,
        Err(e) => Conditional::Failed(e.to_string()),
    }
}

const DAYS: [&str; 7] = ["Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"]; // 1970-01-01 was a Thursday
const MONTHS: [&str; 12] = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

/// Days since 1970-01-01 to (year, month 1-12, day), the civil calendar algorithm by Howard Hinnant.
fn civil(days: i64) -> (i64, usize, i64) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    (yoe + era * 400 + i64::from(month <= 2), month as usize, day)
}

fn days_from_civil(year: i64, month: i64, day: i64) -> i64 {
    let year = if month <= 2 { year - 1 } else { year };
    let era = year.div_euclid(400);
    let yoe = year - era * 400;
    let doy = (153 * (if month > 2 { month - 3 } else { month + 9 }) + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// "2024-05-17" for a time in seconds since 1970 (UTC).
pub fn iso_date(seconds: i64) -> String {
    let (year, month, day) = civil(seconds.div_euclid(86_400));
    format!("{year:04}-{month:02}-{day:02}")
}

/// "Sun, 06 Nov 1994 08:49:37 GMT"
pub fn httpdate(time: SystemTime) -> String {
    let seconds = time.duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs() as i64;
    let (days, rest) = (seconds.div_euclid(86_400), seconds.rem_euclid(86_400));
    let (year, month, day) = civil(days);
    format!("{}, {day:02} {} {year} {:02}:{:02}:{:02} GMT", DAYS[days.rem_euclid(7) as usize], MONTHS[month - 1], rest / 3600, rest / 60 % 60, rest % 60)
}

pub fn parse_httpdate(text: &str) -> Option<SystemTime> {
    let parts: Vec<&str> = text.split_whitespace().collect();
    let [_, day, month, year, clock, "GMT"] = parts.as_slice() else { return None };
    let month = MONTHS.iter().position(|m| m == month)? as i64 + 1;
    let hms: Vec<i64> = clock.split(':').map(|p| p.parse().ok()).collect::<Option<_>>()?;
    let [h, m, s] = hms.as_slice() else { return None };
    let days = days_from_civil(year.parse().ok()?, month, day.parse().ok()?);
    let seconds = days * 86_400 + h * 3600 + m * 60 + s;
    Some(SystemTime::UNIX_EPOCH + Duration::from_secs(u64::try_from(seconds).ok()?))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rate_limit_message_counts_minutes() {
        assert_eq!(rate_limit_message(0), "GitHub's limit of 60 checks an hour is used up; it allows new checks in 1 minute.");
        assert_eq!(rate_limit_message(61), "GitHub's limit of 60 checks an hour is used up; it allows new checks in 2 minutes.");
    }

    #[test]
    fn github_answers_are_reused_and_stand_in_when_github_fails() {
        let cache = tempfile::tempdir().unwrap();
        let url = "http://127.0.0.1:9/never"; // Nothing listens on port 9: every request fails
        assert!(github_api(cache.path(), url).is_err());
        save(&cache_path(cache.path(), url), br#"{"tag_name":"v1"}"#).unwrap();
        assert_eq!(github_api(cache.path(), url).unwrap(), br#"{"tag_name":"v1"}"#); // Fresh: reused
        let old = SystemTime::now() - Duration::from_secs(7200);
        fs::File::options().write(true).open(cache_path(cache.path(), url)).unwrap().set_modified(old).unwrap();
        // Stale, but GitHub failed: the saved copy stands in
        assert_eq!(github_api(cache.path(), url).unwrap(), br#"{"tag_name":"v1"}"#);
    }

    #[test]
    fn http_dates_round_trip() {
        let time = SystemTime::UNIX_EPOCH + Duration::from_secs(784_111_777);
        assert_eq!(httpdate(time), "Sun, 06 Nov 1994 08:49:37 GMT");
        assert_eq!(parse_httpdate("Sun, 06 Nov 1994 08:49:37 GMT"), Some(time));
        assert_eq!(httpdate(SystemTime::UNIX_EPOCH + Duration::from_secs(1_772_323_200)), "Sun, 01 Mar 2026 00:00:00 GMT"); // After a leap-year February
        assert_eq!(parse_httpdate("yesterday"), None);
        assert_eq!(iso_date(1_715_904_000), "2024-05-17");
    }

    #[test]
    fn saved_answers_are_used_for_a_day_and_offline() {
        let cache = tempfile::tempdir().unwrap();
        // A saved answer for an address that cannot answer: fresh, it is returned without asking
        let url = "http://127.0.0.1:9/woofstore-test.json";
        save(&cache_path(cache.path(), url), br#"{"saved":true}"#).unwrap();
        assert_eq!(cached_json(cache.path(), url), br#"{"saved":true}"#);
        // Old, the request is tried, fails, and the old copy is still returned
        let old = SystemTime::now() - Duration::from_secs(3 * 24 * 3600);
        fs::File::options().write(true).open(cache_path(cache.path(), url)).unwrap().set_modified(old).unwrap();
        assert_eq!(cached_json(cache.path(), url), br#"{"saved":true}"#);
        forget(cache.path());
        assert!(cached_json(cache.path(), url).is_empty());
    }
}
