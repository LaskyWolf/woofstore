#pragma once
#include "catalog.h"
#include <QHash>
#include <QSet>

// Hand-written corrections from metadata/merge-overrides.json.
struct MergeOverrides {
    QHash<QString, QString> merge;  // package name -> AppStream ID of the entry it belongs to
    QSet<QString> separate;         // package names that are never merged automatically
};

struct MergeStats {
    int byId = 0, byName = 0, byHomepage = 0, byOverride = 0;
    int multiSource = 0;     // entries offered by two or more sources
    QStringList nearMisses;  // same name as an app but a different project: review by hand
    QStringList merges;      // "rule: package -> entry", for reviewing merges by hand
};

QString stripVariantSuffix(const QString &name);  // "krita-git" -> "krita"
QString nameKey(const QString &text);             // "Visual Studio Code" -> "visualstudiocode"
QString projectKey(const QString &homepage);      // a project's identity: host, or host/owner/repo on code forges
QSet<QString> identityTokens(const QString &url);  // projectKey plus "name:<publisher>" tokens
MergeOverrides loadMergeOverrides(const QStringList &dirs);
// Folds entries that are the same app into one entry; every package stays listed separately.
QList<CatalogApp> mergeCatalog(QList<CatalogApp> apps, const MergeOverrides &overrides, MergeStats *stats);
