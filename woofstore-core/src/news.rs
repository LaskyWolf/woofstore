//! The News page: the feeds it reads, turning RSS and the CachyOS blog page into news items, and the topics the filter offers.
//! The network layer fetches each [`NEWS_SOURCES`] entry and hands the bytes to [`parse_feed`] or [`parse_cachy_blog`].

use chrono::{DateTime, FixedOffset, Local, NaiveDate, TimeZone};
use quick_xml::events::{BytesStart, Event};
use quick_xml::Reader;
use regex::Regex;
use std::sync::LazyLock;

/// One article. `image`: address of the cover picture, empty when the feed has none. `date`: `None` when the feed gives none
/// or one that does not parse, so it sorts after every dated item.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct NewsItem {
    pub source: String,
    pub title: String,
    pub summary: String,
    pub url: String,
    pub date: Option<DateTime<FixedOffset>>,
    pub important: bool,
    pub image: String,
    pub topics: Vec<&'static str>,
}

/// A place the News page reads. `important` items are highlighted; `html` sources are web pages read by [`parse_cachy_blog`]
/// instead of feeds. Each source's own topic lives in [`news_topics`], keyed by `name`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NewsSource {
    pub name: &'static str,
    pub url: &'static str,
    pub important: bool,
    pub html: bool,
}

const fn source(name: &'static str, url: &'static str) -> NewsSource {
    NewsSource { name, url, important: false, html: false }
}

pub const NEWS_SOURCES: &[NewsSource] = &[
    NewsSource { name: "Arch Linux", url: "https://archlinux.org/feeds/news/", important: true, html: false },
    NewsSource { name: "CachyOS", url: "https://cachyos.org/blog/", important: true, html: true },
    source("Linux kernel", "https://www.kernel.org/feeds/kdist.xml"),
    source("KDE", "https://kde.org/announcements/index.xml"),
    source("LWN.net", "https://lwn.net/headlines/rss"),
    source("9to5Linux", "https://9to5linux.com/feed"),
    source("Phoronix", "https://www.phoronix.com/rss.php"),
    source("GamingOnLinux", "https://www.gamingonlinux.com/article_rss.php"),
    source("OMG! Ubuntu", "https://www.omgubuntu.co.uk/feed"),
    source("Fedora Magazine", "https://fedoramagazine.org/feed/"),
    source("Linuxiac", "https://linuxiac.com/feed/"),
    source("It's FOSS", "https://itsfoss.com/news/rss/"),
    source("Linux Mint", "https://blog.linuxmint.com/?feed=rss2"),
    source("Blender", "https://www.blender.org/feed/"),
    source("Krita", "https://krita.org/en/feed/"),
    source("Godot", "https://godotengine.org/rss.xml"),
    source("LibreOffice", "https://blog.documentfoundation.org/feed/"),
    source("Flathub", "https://docs.flathub.org/blog/rss.xml"),
];

/// Only the newest items of each source are kept, so one busy site cannot fill the page.
pub const NEWS_PER_SOURCE: usize = 12;

/// The topics in the order the filter shows them.
pub const NEWS_TOPIC_NAMES: [&str; 6] = ["Security", "Gaming", "Desktop", "Hardware", "Apps", "Linux"];

/// Collapses every run of whitespace to one space and trims the ends, like `QString::simplified`.
fn simplified(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// HTML as the text a reader sees: tags gone, entities decoded, blocks (paragraphs, list items, headings) on their own lines,
/// `<br>` as a line break and pictures as U+FFFC. Follows what `QTextDocument::toPlainText` gives for the HTML feeds and
/// AppStream use; tables and nested blocks are simpler here.
pub fn plain_description(html: &str) -> String {
    #[derive(Default)]
    struct Text {
        out: String,
        open: bool, // the current block has content
        brk: bool,  // a block ended or started since the last content
        pre: bool,
    }
    impl Text {
        fn content(&mut self) {
            if self.brk && self.open {
                self.out.push('\n');
            }
            self.brk = false;
            self.open = true;
        }
        fn push(&mut self, text: &str) {
            for c in html_escape::decode_html_entities(text).chars() {
                if !self.pre && matches!(c, ' ' | '\t' | '\n' | '\r' | '\x0C') {
                    if self.open && !self.brk && !self.out.ends_with([' ', '\n']) {
                        self.out.push(' ');
                    }
                } else {
                    self.content();
                    self.out.push(if c == '\u{A0}' { ' ' } else { c });
                }
            }
        }
    }

    let mut text = Text::default();
    let mut rest = html;
    while let Some(at) = rest.find('<') {
        text.push(&rest[..at]);
        rest = &rest[at..];
        if let Some(comment) = rest.strip_prefix("<!--") {
            rest = comment.find("-->").map_or("", |end| &comment[end + 3..]);
            continue;
        }
        // Like Qt, every '<' opens a tag, up to the next '>' outside quotes.
        let mut quote = None;
        let end = rest
            .char_indices()
            .skip(1)
            .find(|&(_, c)| match quote {
                Some(q) => {
                    if c == q {
                        quote = None;
                    }
                    false
                }
                None if c == '"' || c == '\'' => {
                    quote = Some(c);
                    false
                }
                None => c == '>',
            })
            .map_or(rest.len(), |(i, _)| i + 1);
        let tag = rest[1..end].trim_start();
        rest = &rest[end..];
        let closing = tag.starts_with('/');
        let name = tag.trim_start_matches('/').split(|c: char| !c.is_ascii_alphanumeric()).next().unwrap_or("").to_ascii_lowercase();
        match name.as_str() {
            "script" | "style" | "head" | "title" if !closing && !tag.ends_with('/') => {
                let close = format!("</{name}");
                rest = rest.to_ascii_lowercase().find(&close).map_or("", |at| rest[at..].find('>').map_or("", |end| &rest[at + end + 1..]));
            }
            "br" => {
                text.content();
                text.out.push('\n');
            }
            "img" => text.push("\u{FFFC}"),
            "hr" => {
                text.brk = true;
                text.content();
                text.brk = true;
            }
            "pre" => {
                text.pre = !closing;
                text.brk = true;
            }
            "p" | "div" | "ul" | "ol" | "li" | "dl" | "dt" | "dd" | "h1" | "h2" | "h3" | "h4" | "h5" | "h6" | "blockquote" | "center" | "table" | "tr"
            | "td" | "th" => text.brk = true,
            _ => {}
        }
    }
    text.push(rest);
    text.out
}

/// The first picture in a piece of HTML.
fn first_image(html: &str) -> String {
    static TAG: LazyLock<Regex> = LazyLock::new(|| Regex::new(r#"<img[^>]+src="(https?://[^"]+)""#).unwrap());
    TAG.captures(html).map_or_else(String::new, |c| c[1].to_string())
}

/// WordPress sites link small copies ("name-300x127.jpg"); the original has no size in its name.
fn full_size_image(url: String) -> String {
    // `\n?\z` is what `$` means in Qt's regular expressions: the end, or just before a final newline.
    static SIZE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"-[0-9]+x[0-9]+(\.(?:jpe?g|png|webp))(\n?)\z").unwrap());
    if url.contains("/wp-content/uploads/") {
        SIZE.replace_all(&url, "$1$2").into_owned()
    } else {
        url
    }
}

/// Topics of an article: its source's own topic, then every topic whose words appear in the title, summary or the feed's
/// categories; "Linux" when nothing matched.
pub fn news_topics<S: AsRef<str>>(source: &str, title: &str, summary: &str, feed_categories: &[S]) -> Vec<&'static str> {
    // A word only counts at its start ("game" in "endgame" does not); `regex` has no look-around, and consuming the
    // neighbouring character gives the same yes/no answer.
    static RULES: LazyLock<Vec<(&str, Regex)>> = LazyLock::new(|| {
        let rule = |topic, words: &str| (topic, Regex::new(&format!("(?i)(?:^|[^a-z])(?:{})", words.replace("(?![a-z])", "(?:[^a-z]|$)"))).unwrap());
        vec![
            rule("Security", r"security|cve-[0-9]|vulnerab|exploit|malware|ransomware|backdoor|zero-day|0-day|privilege escalation|breach|phishing|cyber"),
            rule("Gaming", r"gam(?:e|es|ing)(?![a-z])|steam|proton|wine(?![a-z])|lutris|heroic|gamescope|vulkan|esports|gog(?![a-z])|valve"),
            rule("Desktop", r"kde|gnome|plasma|xfce|cinnamon|cosmic|wayland|hyprland|x11|desktop|wm(?![a-z])"),
            rule("Linux", r"kernel|linux [0-9]+\.[0-9]+|ubuntu|fedora|debian|arch linux|manjaro|opensuse|linux mint|nixos|cachyos|distro|endeavouros|pop!_os"),
            rule("Hardware", r"hardware|amd|nvidia|intel|gpu|radeon|mesa|laptop|risc-v|raspberry|firmware|driver"),
            rule("Apps", r"firefox|thunderbird|libreoffice|vlc|obs studio|gimp|krita|blender|chrome|browser|flatpak|appimage"),
        ]
    });
    let by_source = match source {
        "GamingOnLinux" => Some("Gaming"),
        "KDE" => Some("Desktop"),
        "Linux kernel" | "Arch Linux" | "CachyOS" | "Linux Mint" | "Fedora Magazine" | "OMG! Ubuntu" => Some("Linux"),
        "Blender" | "Krita" | "Godot" | "LibreOffice" | "Flathub" => Some("Apps"),
        _ => None,
    };
    let categories: Vec<&str> = feed_categories.iter().map(AsRef::as_ref).collect();
    let text = format!("{title} {summary} {}", categories.join(" "));
    let mut topics: Vec<&'static str> = by_source.into_iter().collect();
    for (topic, words) in RULES.iter() {
        if !topics.contains(topic) && words.is_match(&text) {
            topics.push(topic);
        }
    }
    if topics.is_empty() {
        topics.push("Linux"); // Kernel, distributions and everything else
    }
    topics
}

/// An RSS date ("Mon, 13 Oct 2025 12:00:00 +0000"); one without a zone is UTC, as Qt reads it.
fn rss_date(text: &str) -> Option<DateTime<FixedOffset>> {
    DateTime::parse_from_rfc2822(text).or_else(|_| DateTime::parse_from_rfc2822(&format!("{text} +0000"))).ok()
}

/// The text inside the element just opened, or `None` when it holds another element or the XML breaks.
fn element_text(reader: &mut Reader<&[u8]>, empty: bool) -> Option<String> {
    let mut text = String::new();
    if empty {
        return Some(text);
    }
    loop {
        match reader.read_event().ok()? {
            Event::Text(t) => text += &t.unescape().ok()?,
            Event::CData(c) => text += &reader.decoder().decode(&c).ok()?,
            Event::Comment(_) | Event::PI(_) => {}
            Event::End(_) => return Some(text),
            _ => return None,
        }
    }
}

/// Every attribute must be well formed, as Qt's reader checks them all.
fn attributes_ok(element: &BytesStart) -> bool {
    element.attributes().all(|a| a.is_ok_and(|a| a.unescape_value().is_ok()))
}

fn attribute(element: &BytesStart, name: &str) -> String {
    element.try_get_attribute(name).ok().flatten().and_then(|a| a.unescape_value().ok()).map(|v| v.into_owned()).unwrap_or_default()
}

/// The summary as plain text of at most 220 UTF-16 units (Qt's string length), cut with an ellipsis.
fn short_summary(html: &str) -> String {
    let text = simplified(&plain_description(html).replace('\u{FFFC}', ""));
    if text.encode_utf16().count() <= 220 {
        return text;
    }
    let mut units = 0;
    let cut: String = text.chars().take_while(|c| (units += c.len_utf16(), units <= 217).1).collect();
    cut.trim().to_string() + "…"
}

/// Items of an RSS feed. Element names are matched without their namespace prefix, so `content:encoded`, `media:content` and
/// `media:thumbnail` count. Items without a title or an http(s) link are dropped. The feed stops at the first XML error,
/// keeping the items before it.
pub fn parse_feed(xml: &[u8], source: &str, important: bool) -> Vec<NewsItem> {
    static PICTURE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\.(jpe?g|png|webp)(\?|\n?\z)").unwrap());
    let mut reader = Reader::from_reader(xml);
    let mut items = Vec::new();
    let mut item = NewsItem::default();
    let mut tags: Vec<String> = Vec::new();
    let mut in_item = false;
    loop {
        let (element, empty) = match reader.read_event() {
            Ok(Event::Start(e)) => (e, false),
            Ok(Event::Empty(e)) => (e, true),
            Ok(Event::End(e)) if e.local_name().as_ref() == b"item" => {
                in_item = false;
                items.extend(finish(std::mem::take(&mut item), &tags));
                continue;
            }
            Ok(Event::Text(t)) if t.unescape().is_err() => break,
            Ok(Event::Eof) | Err(_) => break,
            _ => continue,
        };
        if !attributes_ok(&element) {
            break;
        }
        let name = element.local_name();
        // Qt's reader stops the whole feed when an element's text is broken or holds another element.
        macro_rules! text {
            () => {
                match element_text(&mut reader, empty) {
                    Some(text) => text,
                    None => break,
                }
            };
        }
        match name.as_ref() {
            b"item" => {
                in_item = true;
                item = NewsItem { source: source.into(), important, ..Default::default() };
                tags.clear();
                if empty {
                    in_item = false;
                    items.extend(finish(std::mem::take(&mut item), &tags));
                }
            }
            _ if !in_item => {}
            b"category" => tags.push(simplified(&text!())),
            b"title" => item.title = simplified(&text!()),
            b"link" => item.url = text!().trim().into(),
            b"description" => item.summary = text!(),
            b"encoded" => {
                let html = text!();
                if item.image.is_empty() {
                    item.image = first_image(&html);
                }
            }
            b"enclosure" | b"content" | b"thumbnail" if item.image.is_empty() => {
                let (url, kind, medium) = (attribute(&element, "url"), attribute(&element, "type"), attribute(&element, "medium"));
                let picture = name.as_ref() == b"thumbnail" || kind.starts_with("image/") || medium == "image" || (kind.is_empty() && PICTURE.is_match(&url));
                if picture && url.starts_with("http") {
                    item.image = url;
                }
            }
            b"pubDate" => item.date = rss_date(text!().trim()),
            _ => {}
        }
    }
    items
}

fn finish(mut item: NewsItem, tags: &[String]) -> Option<NewsItem> {
    if item.image.is_empty() {
        item.image = first_image(&item.summary);
    }
    item.image = full_size_image(item.image);
    item.summary = short_summary(&item.summary);
    // Some feeds (KDE's announcements) leave the title empty: the summary's first sentence names the article
    if item.title.trim().is_empty() {
        let first = item.summary.split_inclusive(". ").next().unwrap_or("").trim().trim_end_matches('…');
        item.title = if first.chars().count() > 100 { format!("{}…", first.chars().take(99).collect::<String>().trim_end()) } else { first.to_string() };
    }
    item.topics = news_topics(&item.source, &item.title, &item.summary, tags);
    (!item.title.is_empty() && item.url.starts_with("http")).then_some(item)
}

/// Posts on the CachyOS blog page, which has no feed: each is `<time>date</time> … <h2><a href="/blog/slug">Title</a></h2> … <p>Summary</p>`.
pub fn parse_cachy_blog(html: &str) -> Vec<NewsItem> {
    // `[\t\n\x0B\x0C\r ]` is Qt's `\s`, which leaves Unicode spaces out.
    static POST: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r#"(?s)<time[^>]*>([^<]*)</time>.*?<h2[^>]*>[\t\n\x0B\x0C\r ]*<a[^>]*href="(/blog/[^"]+)"[^>]*>([^<]+)</a>[\t\n\x0B\x0C\r ]*</h2>.*?<p[^>]*>([^<]*)</p>"#)
            .unwrap()
    });
    POST.captures_iter(html)
        .map(|c| {
            let title = simplified(&plain_description(&c[3]));
            let summary = simplified(&plain_description(&c[4]));
            let topics = news_topics::<&str>("CachyOS", &title, &summary, &[]);
            let date = NaiveDate::parse_from_str(c[1].trim(), "%b %d, %Y")
                .ok()
                .and_then(|day| Local.from_local_datetime(&day.and_hms_opt(0, 0, 0)?).earliest())
                .map(|time| time.fixed_offset());
            NewsItem {
                source: "CachyOS".into(),
                title,
                summary,
                url: format!("https://cachyos.org{}", &c[2]),
                date,
                important: true,
                image: String::new(),
                topics,
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn feeds() {
        let feed = parse_feed(b"<rss><channel><item><title>Manual intervention needed</title><link>https://archlinux.org/news/x/</link><pubDate>Mon, 13 Oct 2025 12:00:00 +0000</pubDate><description>&lt;p&gt;Do this &lt;b&gt;first&lt;/b&gt;.&lt;/p&gt;</description></item><item><title>No link</title></item></channel></rss>", "Arch Linux", true);
        assert_eq!(feed.len(), 1);
        assert!(feed[0].summary == "Do this first." && feed[0].important && feed[0].source == "Arch Linux");
        assert_eq!(feed[0].date.unwrap().date_naive(), NaiveDate::from_ymd_opt(2025, 10, 13).unwrap());
    }

    #[test]
    fn topics() {
        let none: &[&str] = &[];
        assert_eq!(news_topics("Blender", "Blender 5.1 released", "", none), ["Apps"]);
        assert_eq!(news_topics("GamingOnLinux", "Hades II arrives", "", none), ["Gaming"]);
        assert_eq!(news_topics("Phoronix", "Linux 7.2 fixes a privilege escalation flaw in the kernel", "", none), ["Security", "Linux"]);
        assert!(news_topics("LWN.net", "A new release", "", &["Hardware"]).contains(&"Hardware"));
        assert_eq!(news_topics("LWN.net", "A look at sched_ext", "", none), ["Linux"]);
        assert_eq!(news_topics("Arch Linux", "Manual intervention", "", none), ["Linux"]);
        assert!(!news_topics("LWN.net", "Programming with Rust", "", none).contains(&"Gaming"));
        assert!(!news_topics("LWN.net", "Teamwork in open source", "", none).contains(&"Hardware"));
        let tagged = parse_feed(
            b"<rss><channel><item><title>A story</title><link>https://x.org/a</link><category>Security</category></item></channel></rss>",
            "9to5Linux",
            false,
        );
        assert_eq!(tagged.len(), 1);
        assert_eq!(tagged[0].topics, ["Security"]);
    }

    #[test]
    fn rss_dates() {
        assert_eq!(rss_date("13 Oct 2025 12:00 +0200").unwrap().to_rfc3339(), "2025-10-13T12:00:00+02:00");
        assert_eq!(rss_date("Mon, 13 Oct 2025 12:00:00").unwrap().to_rfc3339(), "2025-10-13T12:00:00+00:00");
        assert_eq!(rss_date("Tue, 13 Oct 2025 12:00:00 +0000"), None); // the day name must agree
        assert_eq!(rss_date("13 October 2025"), None);
    }

    #[test]
    fn cachy_blog() {
        let blog = parse_cachy_blog("<li><article><time datetime=\"2026-08-09\">Aug 9, 2026</time> · <a href=\"/category/release\">release</a><h2 class=\"x\"><a class=\"y\" href=\"/blog/2608-august-release\">CachyOS August 2026 Release</a></h2><p class=\"z\">Shelly Rewrite</p></article></li>");
        assert_eq!(blog.len(), 1);
        assert_eq!(blog[0].url, "https://cachyos.org/blog/2608-august-release");
        assert_eq!(blog[0].date.unwrap().date_naive(), NaiveDate::from_ymd_opt(2026, 8, 9).unwrap());
        assert_eq!(blog[0].summary, "Shelly Rewrite");
    }

    // Expected values below are what QTextDocument::toPlainText gives for the same input.
    #[test]
    fn plain_text() {
        for (html, text) in [
            ("<p>a</p><p>b</p>", "a\nb"),
            ("<p> a  b </p>\n<p>c</p>", "a b \nc"),
            ("x<br>y", "x\ny"),
            ("x<br/>", "x\n"),
            ("<ul><li>one</li><li>two</li></ul>", "one\ntwo"),
            ("<p>a</p><p></p><p>b</p>", "a\nb"),
            ("a &amp; &nbsp;b &eacute; &#233; &hellip; &apos; &rsquo;", "a &  b é é … ' ’"),
            ("<img src=\"x\">pic", "\u{FFFC}pic"),
            ("<script>bad()</script>ok<style>p{}</style>", "ok"),
            ("<pre>a\n  b</pre>c", "a\n  b\nc"),
            ("a <b>b</b> c", "a b c"),
            ("<h1>T</h1>body", "T\nbody"),
            ("<section>a</section><article>b</article>", "ab"),
            ("1 < 2 and 3>2", "1 2"),
            ("<!-- c -->x", "x"),
            ("a\tb\r\nc", "a b c"),
            ("<title>t</title>z", "z"),
            ("<p>a<br></p><p>b</p>", "a\n\nb"),
            ("AT&T", "AT&T"),
            (" lead", "lead"),
            ("a<hr>b", "a\n\nb"),
            ("x<br> y", "x\ny"),
            ("a <p> b", "a \nb"),
            ("a < b", "a "),
            ("<a title=\"x>y\">t</a>", "t"),
            ("<P>A</P><P>B</P>", "A\nB"),
            ("<i>a</i>\n<i>b</i>", "a b"),
            ("<p>a</p>tail <p>b</p>", "a\ntail \nb"),
        ] {
            assert_eq!(plain_description(html), text, "{html:?}");
        }
    }

    #[test]
    fn images() {
        assert_eq!(first_image(r#"<p><img class="a" src="https://x.org/a.png"></p><img src="https://x.org/b.png">"#), "https://x.org/a.png");
        assert_eq!(first_image(r#"<img src="/relative.png">"#), "");
        assert_eq!(full_size_image("https://x.org/wp-content/uploads/a-300x127.jpg".into()), "https://x.org/wp-content/uploads/a.jpg");
        assert_eq!(full_size_image("https://x.org/a-300x127.jpg".into()), "https://x.org/a-300x127.jpg");
        let feed = parse_feed(
            br#"<rss xmlns:media="m"><channel><item><title>T</title><link>https://x.org/a</link><media:content url="https://x.org/wp-content/uploads/p-640x360.webp" medium="image"/></item></channel></rss>"#,
            "Linuxiac",
            false,
        );
        assert_eq!(feed[0].image, "https://x.org/wp-content/uploads/p.webp");
    }

    #[test]
    fn untitled_items_take_the_summary_start() {
        let xml = br#"<rss><channel><item><title/><link>https://kde.org/a/</link><description>KDE Frameworks 6.32.0 is out. It fixes bugs.</description></item></channel></rss>"#;
        assert_eq!(parse_feed(xml, "KDE", false)[0].title, "KDE Frameworks 6.32.0 is out.");
    }

    #[test]
    fn broken_feeds_keep_earlier_items() {
        let feed = parse_feed(
            b"<rss><item><title>A</title><link>https://x.org/a</link></item><item><title>B &nbsp;</title><link>https://x.org/b</link></item></rss>",
            "LWN.net",
            false,
        );
        assert_eq!(feed.len(), 1);
        assert_eq!(feed[0].title, "A");
    }

    #[test]
    fn long_summaries_are_cut() {
        let summary = parse_feed(
            format!("<rss><item><title>T</title><link>https://x.org</link><description>{}</description></item></rss>", "word ".repeat(60)).as_bytes(),
            "LWN.net",
            false,
        )
        .remove(0)
        .summary;
        assert!(summary.ends_with(" wo…") && summary.chars().count() == 218, "{summary}");
    }
}
