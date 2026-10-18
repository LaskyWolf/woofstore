#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

// What the AUR knows about a package; shown to the user next to the recipe. Facts, not a score.
struct AurInfo {
    QString name, base, maintainer, url;  // Empty maintainer: orphaned
    QDateTime firstSubmitted, lastModified, outOfDate;
    int votes = 0;
    double popularity = 0;
};

// First result of an AUR RPC v5 "info" reply.
AurInfo parseAurInfo(const QByteArray &json, bool *ok = nullptr);

// Things in a recipe a reviewer should read twice. recipe is Backend::prepareAur's text ("──── file ────" headers).
// These point at lines; an empty list never means the recipe is safe.
QStringList reviewFlags(const QString &recipe, const QString &upstreamUrl);

// The recipe the user last approved for each package base, kept to show what changed on the next update.
bool saveReviewed(const QString &dir, const QString &base, const QString &commit, const QString &recipe);
QString loadReviewed(const QString &dir, const QString &base, QString *commit = nullptr);
