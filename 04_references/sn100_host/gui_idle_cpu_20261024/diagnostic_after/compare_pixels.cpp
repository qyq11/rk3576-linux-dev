#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <algorithm>
#include <cmath>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 8) return 2;
    QImage before(argv[1]), after(argv[2]);
    if (before.isNull() || before.size() != after.size()) return 3;
    const QRect excluded(QString(argv[4]).toInt(), QString(argv[5]).toInt(),
                         QString(argv[6]).toInt(), QString(argv[7]).toInt());
    qint64 changed = 0, changedOutside = 0, channels = 0, absoluteOutside = 0;
    int maxOutside = 0;
    QRect allBounds, outsideBounds;
    for (int y = 0; y < before.height(); ++y)
    {
        for (int x = 0; x < before.width(); ++x)
        {
            const QColor a = before.pixelColor(x, y), b = after.pixelColor(x, y);
            const bool different = a != b;
            if (different)
            {
                ++changed;
                allBounds |= QRect(x, y, 1, 1);
            }
            if (excluded.contains(x, y)) continue;
            channels += 4;
            for (int delta : {std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                              std::abs(a.blue() - b.blue()), std::abs(a.alpha() - b.alpha())})
            {
                absoluteOutside += delta;
                maxOutside = std::max(maxOutside, delta);
            }
            if (different)
            {
                ++changedOutside;
                outsideBounds |= QRect(x, y, 1, 1);
            }
        }
    }
    auto rect = [](QRect value) { return QJsonArray{value.x(), value.y(), value.width(), value.height()}; };
    QJsonObject result{{"width", before.width()}, {"height", before.height()},
                       {"analysis_excluded_rect_xywh", rect(excluded)},
                       {"changed_pixels_all", double(changed)}, {"changed_bounds_all_xywh", rect(allBounds)},
                       {"changed_pixels_outside_rect", double(changedOutside)},
                       {"changed_bounds_outside_rect_xywh", rect(outsideBounds)},
                       {"maximum_channel_delta_outside_rect", maxOutside},
                       {"mean_absolute_channel_delta_outside_rect", channels ? double(absoluteOutside) / channels : 0},
                       {"images_rewritten", false}, {"mask_is_numeric_only", true}};
    QFile file(argv[3]);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 4;
    return file.write(QJsonDocument(result).toJson()) > 0 ? 0 : 5;
}
