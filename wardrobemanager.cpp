#include "wardrobemanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QUuid>
#include <opencv2/opencv.hpp>

QString WardrobeManager::wardrobeDir() { return "images/wardrobe"; }
QString WardrobeManager::thumbsDir() { return "images/wardrobe/thumbs"; }

QString WardrobeManager::uniqueFileName(const QString &ext) {
    return QString("shirt_%1_%2.%3")
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8))
        .arg(ext);
}

void WardrobeManager::init() {
    QDir().mkpath(wardrobeDir());
    QDir().mkpath(thumbsDir());
    QSqlQuery q;
    q.exec("CREATE TABLE IF NOT EXISTS wardrobe ("
           "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "  name TEXT, category TEXT, image_path TEXT, thumb_path TEXT,"
           "  color TEXT, tags TEXT, created_at TEXT)");
}

int WardrobeManager::getItemCount() {
    QSqlQuery q("SELECT COUNT(*) FROM wardrobe");
    if (q.next()) return q.value(0).toInt();
    return 0;
}

QString WardrobeManager::generateThumbnail(const QString &srcPath) {
    cv::Mat img = cv::imread(srcPath.toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) return "";
    int maxSize = 200;
    double scale = 1.0;
    if (img.cols > maxSize || img.rows > maxSize)
        scale = (double)maxSize / std::max(img.cols, img.rows);

    cv::Mat thumb;
    if (scale < 1.0) cv::resize(img, thumb, cv::Size(), scale, scale, cv::INTER_AREA);
    else thumb = img.clone();

    QString ext = QFileInfo(srcPath).suffix().toLower();
    if (ext.isEmpty()) ext = "png";
    QString thumbPath = thumbsDir() + "/" + QFileInfo(srcPath).baseName() + "_thumb." + ext;
    cv::imwrite(thumbPath.toStdString(), thumb);
    return thumbPath;
}

int WardrobeManager::addItem(const QString &srcImagePath, const QString &name, const QString &category, const QString &color, const QString &tags) {
    if (!QFile::exists(srcImagePath)) return -1;
    QDir().mkpath(wardrobeDir());
    QString ext = QFileInfo(srcImagePath).suffix().toLower();
    if (ext.isEmpty()) ext = "png";
    QString dstPath = wardrobeDir() + "/" + uniqueFileName(ext);

    if (!QFile::copy(srcImagePath, dstPath)) return -1;
    QString thumbPath = generateThumbnail(dstPath);

    QSqlQuery q;
    q.prepare("INSERT INTO wardrobe (name, category, image_path, thumb_path, color, tags, created_at) VALUES (?,?,?,?,?,?,?)");
    q.addBindValue(name); q.addBindValue(category); q.addBindValue(dstPath);
    q.addBindValue(thumbPath); q.addBindValue(color); q.addBindValue(tags);
    q.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    if (!q.exec()) return -1;
    return q.lastInsertId().toInt();
}

bool WardrobeManager::removeItem(int id) {
    WardrobeItem item = getItemById(id);
    if (item.id < 0) return false;
    QFile::remove(item.imagePath);
    QFile::remove(item.thumbPath);
    QSqlQuery q; q.prepare("DELETE FROM wardrobe WHERE id=?"); q.addBindValue(id);
    return q.exec();
}

bool WardrobeManager::updateItem(int id, const QString &name, const QString &category, const QString &color, const QString &tags) {
    QSqlQuery q;
    q.prepare("UPDATE wardrobe SET name=?, category=?, color=?, tags=? WHERE id=?");
    q.addBindValue(name); q.addBindValue(category); q.addBindValue(color); q.addBindValue(tags); q.addBindValue(id);
    return q.exec();
}

static WardrobeItem rowToItem(const QSqlQuery &q) {
    WardrobeItem it;
    it.id = q.value("id").toInt(); it.name = q.value("name").toString();
    it.category = q.value("category").toString(); it.imagePath = q.value("image_path").toString();
    it.thumbPath = q.value("thumb_path").toString(); it.color = q.value("color").toString();
    it.tags = q.value("tags").toString();
    return it;
}

QVector<WardrobeItem> WardrobeManager::getAllItems() {
    QVector<WardrobeItem> res; QSqlQuery q("SELECT * FROM wardrobe ORDER BY id DESC");
    while (q.next()) res.append(rowToItem(q)); return res;
}
QVector<WardrobeItem> WardrobeManager::getItemsByCategory(const QString &category) {
    if (category.isEmpty() || category == "全部") return getAllItems();
    QVector<WardrobeItem> res; QSqlQuery q;
    q.prepare("SELECT * FROM wardrobe WHERE category=? ORDER BY id DESC"); q.addBindValue(category);
    q.exec(); while (q.next()) res.append(rowToItem(q)); return res;
}
QVector<WardrobeItem> WardrobeManager::searchItems(const QString &keyword) {
    if (keyword.isEmpty()) return getAllItems();
    QVector<WardrobeItem> res; QSqlQuery q;
    q.prepare("SELECT * FROM wardrobe WHERE name LIKE ? OR tags LIKE ? ORDER BY id DESC");
    q.addBindValue("%"+keyword+"%"); q.addBindValue("%"+keyword+"%");
    q.exec(); while (q.next()) res.append(rowToItem(q)); return res;
}
WardrobeItem WardrobeManager::getItemById(int id) {
    WardrobeItem it; QSqlQuery q; q.prepare("SELECT * FROM wardrobe WHERE id=?"); q.addBindValue(id);
    q.exec(); if (q.next()) it = rowToItem(q); return it;
}
QStringList WardrobeManager::getCategories() {
    QStringList list; list << "全部" << "衬衫" << "连衣裙" << "外套" << "T恤" << "其他";
    QSqlQuery q("SELECT DISTINCT category FROM wardrobe WHERE category != ''");
    while (q.next()) { QString c = q.value(0).toString(); if (!list.contains(c)) list << c; }
    return list;
}