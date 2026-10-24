#ifndef SENSMEMODEL_H
#define SENSMEMODEL_H

#include <QtCore/QVector>

#include "screenmodel.h"

class RowModel;

// SensMe Channels: the channels the library's 12 TONE ANALYSIS puts tracks
// in, on a strip of thumbnails; landing on one plays it shuffled, the first
// track from its highlight. Slots are the stock strip's 14 places.
class SensMePlayerModel : public ScreenModel
{
    Q_OBJECT
public:
    SensMePlayerModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;
    void entering() override;

    static const int slotCount = 14;

protected:
    void attached() override { showCurrent(); }

private:
    void rebuild();
    void refresh();
    void showCurrent();
    void play(int slot);

    RowModel *m_list;
    QVector<int> m_members[slotCount];
    QVector<int> m_shown;
    int m_unanalyzed = 0;
    bool m_told = false;
};

#endif
