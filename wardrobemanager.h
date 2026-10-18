#ifndef WARDROBEMANAGER_H
#define WARDROBEMANAGER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

struct WardrobeItem {
    int id = -1;
    QString name;
    QString category;
    QString imagePath;
    QString thumbPath;
    QString color;
    QString tags;
    QDateTime createdAt;
};

class WardrobeManager
{
public:
    static void init();
    static int addItem(const QString &srcImagePath, const QString &name, const QString &category, const QString &color = "", const QString &tags = "");
    static bool removeItem(int id);
    static bool updateItem(int id, const QString &name, const QString &category, const QString &color, const QString &tags);
    static QVector<WardrobeItem> getAllItems();
    static QVector<WardrobeItem> getItemsByCategory(const QString &category);
    static QVector<WardrobeItem> searchItems(const QString &keyword);
    static WardrobeItem getItemById(int id);
    static QStringList getCategories();
    static int getItemCount(); // 获取总数，用于主页展示

private:
    static QString wardrobeDir();
    static QString thumbsDir();
    static QString generateThumbnail(const QString &srcPath);
    static QString uniqueFileName(const QString &ext);
};

#endif // WARDROBEMANAGER_H