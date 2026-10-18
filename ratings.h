#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// What people say about an app, from sources that publish it. -1 / 0 means "not available": the row is hidden.
struct Ratings {
    double stars = 0;            // 1-5, only from ODRS reviews
    int reviews = 0;
    int histogram[5] = {0, 0, 0, 0, 0};  // Reviews with 1..5 stars
    qint64 installs = -1, installsMonth = -1;  // Flathub
    int aurVotes = -1;
    double aurPopularity = 0;
    double repoPercent = -1;     // pkgstats: share of reporting systems
    bool any() const { return reviews > 0 || installs >= 0 || aurVotes >= 0 || repoPercent >= 0; }
};

// ODRS: {"star1": n, ... "star5": n, "total": n}. Fewer than minReviews reviews say nothing, so they give no stars.
inline void parseOdrs(const QByteArray &json, Ratings *out, int minReviews = 5) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const int total = o.value("total").toInt();
    if (total < minReviews) return;
    double sum = 0;
    for (int i = 1; i <= 5; ++i) sum += i * o.value("star" + QString::number(i)).toInt();
    out->stars = sum / total; out->reviews = total;
    for (int i = 0; i < 5; ++i) out->histogram[i] = o.value("star" + QString::number(i + 1)).toInt();
}

// Flathub /api/v2/stats/<id>
inline void parseFlathubStats(const QByteArray &json, Ratings *out) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (!o.contains("installs_total")) return;
    out->installs = qint64(o.value("installs_total").toDouble()); out->installsMonth = qint64(o.value("installs_last_month").toDouble());
}
