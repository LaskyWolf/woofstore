//! The News page's articles, read from every source on a worker thread.

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
        #[qproperty(bool, loading)]
        /// JSON array, newest first: {source, title, summary, url, date (ISO 8601 or ""), important, image, topics}
        #[qproperty(QString, items)]
        /// JSON array of "Source: reason" for sources that gave nothing
        #[qproperty(QString, problems)]
        type News = super::NewsRust;

        #[qinvokable]
        fn load(self: Pin<&mut News>);
    }

    impl cxx_qt::Threading for News {}
}

use core::pin::Pin;
use cxx_qt::Threading;
use cxx_qt_lib::QString;
use serde_json::json;
use woofstore_core::news::news_topics;
use woofstore_system::{http, news};

/// Sample articles for screenshots (WOOFSTORE_PREVIEW contains "sample-news"): no network.
/// Not newest first, so the columns fill in a fixed order.
fn sample_items() -> serde_json::Value {
    let images = concat!(env!("CARGO_MANIFEST_DIR"), "/../tests/fixtures/images");
    let samples = [
        ("Arch Linux", "Manual intervention required for the nvidia 580 driver update", "Packages from the 580 series need the older driver removed first. Remove it before you update, then install nvidia-open again.", "https://archlinux.org/news/", 18, true, String::new()),
        ("CachyOS", "CachyOS October 2026 Release", "New ISO, kernel 7.2 as default and a reworked update tool.", "https://cachyos.org/blog/", 15, true, String::new()),
        ("Linux kernel", "7.2.11: stable", "", "https://kernel.org", 17, false, String::new()),
        ("KDE", "Plasma 6.9 beta is out for testing", "The beta brings a new panel editor, faster window previews and many fixes in the Wayland session. Testers can report problems until the final release.", "https://kde.org/announcements/", 14, false, format!("{images}/org.kde.krita-screenshot.png")),
        ("LWN.net", "A look at the new scheduler extension class", "Sched_ext lets programs written in BPF decide which task runs next.", "https://lwn.net", 13, false, String::new()),
        ("9to5Linux", "Mesa 26.2 released with Vulkan improvements", "The release adds drivers for new GPUs and speeds up shader compilation.", "https://9to5linux.com", 12, false, format!("{images}/org.gimp.GIMP-screenshot.png")),
        ("Phoronix", "Linux 7.2 fixes a privilege escalation flaw", "A local attacker could gain root. Update the kernel.", "https://phoronix.com", 16, false, String::new()),
    ];
    samples
        .iter()
        .map(|(source, title, summary, url, day, important, image)| {
            json!({
                "source": source, "title": title, "summary": summary, "url": url,
                "date": format!("2026-10-{day}T12:00:00"),  // Local time
                "important": important, "image": image, "topics": news_topics(source, title, summary, &[] as &[&str]),
            })
        })
        .collect()
}

pub struct NewsRust {
    loading: bool,
    items: QString,
    problems: QString,
}

impl Default for NewsRust {
    fn default() -> Self {
        Self { loading: false, items: QString::from("[]"), problems: QString::from("[]") }
    }
}

impl qobject::News {
    fn load(mut self: Pin<&mut Self>) {
        if *self.loading() {
            return;
        }
        if std::env::var("WOOFSTORE_PREVIEW").is_ok_and(|p| p.split(',').any(|part| part == "sample-news")) {
            self.as_mut().set_items(QString::from(&sample_items().to_string()));
            self.set_problems(QString::from("[]"));
            return;
        }
        self.as_mut().set_loading(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let (items, problems) = news::load_news(|url| http::get(url, 0));
            let items: Vec<_> = items
                .iter()
                .map(|i| {
                    json!({
                        "source": i.source, "title": i.title, "summary": i.summary, "url": i.url,
                        "date": i.date.map(|d| d.to_rfc3339()).unwrap_or_default(),
                        "important": i.important, "image": i.image, "topics": i.topics,
                    })
                })
                .collect();
            let (items, problems) = (serde_json::Value::from(items).to_string(), serde_json::Value::from(problems).to_string());
            let _ = thread.queue(move |mut news| {
                news.as_mut().set_items(QString::from(&items));
                news.as_mut().set_problems(QString::from(&problems));
                news.set_loading(false);
            });
        });
    }
}
