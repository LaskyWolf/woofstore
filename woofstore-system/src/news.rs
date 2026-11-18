//! The News page: every source read side by side, the newest items first.

use woofstore_core::news::{parse_cachy_blog, parse_feed, NewsItem, NEWS_PER_SOURCE, NEWS_SOURCES};

/// Reads every source with `get` (address -> body or error). Returns the items, newest first, and one problem line per
/// source that gave nothing.
pub fn load_news(get: impl Fn(&str) -> Result<Vec<u8>, String> + Sync) -> (Vec<NewsItem>, Vec<String>) {
    let results: Vec<(Vec<NewsItem>, Option<String>)> = std::thread::scope(|scope| {
        let handles: Vec<_> = NEWS_SOURCES
            .iter()
            .map(|source| {
                let get = &get;
                scope.spawn(move || {
                    let (data, error) = match get(source.url) {
                        Ok(data) => (data, String::new()),
                        Err(error) => (Vec::new(), error),
                    };
                    let mut parsed =
                        if source.html { parse_cachy_blog(&String::from_utf8_lossy(&data)) } else { parse_feed(&data, source.name, source.important) };
                    let problem = parsed.is_empty().then(|| format!("{}: {}", source.name, if error.is_empty() { "nothing found" } else { &error }));
                    parsed.truncate(NEWS_PER_SOURCE);
                    (parsed, problem)
                })
            })
            .collect();
        handles.into_iter().map(|h| h.join().unwrap_or_default()).collect()
    });
    let mut items = Vec::new();
    let mut problems = Vec::new();
    for (parsed, problem) in results {
        items.extend(parsed);
        problems.extend(problem);
    }
    // Undated items sort last: None < Some, so compare reversed
    items.sort_by_key(|item| std::cmp::Reverse(item.date));
    (items, problems)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn newest_first_and_failures_reported() {
        let feed = |date: &str, title: &str| {
            format!("<rss><channel><item><title>{title}</title><link>https://x/{title}</link><pubDate>{date}</pubDate></item></channel></rss>")
        };
        let (items, problems) = load_news(|url| match url {
            "https://archlinux.org/feeds/news/" => Ok(feed("Mon, 02 Nov 2026 10:00:00 GMT", "old").into_bytes()),
            "https://lwn.net/headlines/rss" => Ok(feed("Tue, 10 Nov 2026 10:00:00 GMT", "new").into_bytes()),
            _ => Err("offline".into()),
        });
        let titles: Vec<&str> = items.iter().map(|i| i.title.as_str()).collect();
        assert_eq!(titles, ["new", "old"]);
        assert!(items[1].important);
        assert_eq!(problems.len(), NEWS_SOURCES.len() - 2);
        assert!(problems.iter().any(|p| p == "CachyOS: offline"));
    }
}
