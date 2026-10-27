#define main existingSamplesSuiteMainNotCalled
#include "../../../06_programs/sn100_host/samples/tests/samples_test.cpp"
#undef main
#include "cusbadgebutton.h"
#include "cusracklistwidget.h"
#include <QLabel>
#include <functional>
#include <stdexcept>

static void require(bool ok, const char *message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
static bool until(const std::function<bool()> &predicate)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < 5000)
    {
        QTest::qWait(10);
    }
    return predicate();
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ProjectsStore::registerTypes();
    SampleStore::registerTypes();
    try
    {
        Harness h;
        require(h.start(), "initialize");
        h.actor();
        require(until([&]() { return h.page->isReady() && !h.page->isBusy(); }), "ready");
        auto *racks = h.page->findChild<CusRackListWidget *>("rackList");
        auto *table = h.page->findChild<QTableWidget *>("tblSetting");
        auto *add = h.page->findChild<CusBadgeButton *>("btnAdd");
        auto *previous = h.page->findChild<QPushButton *>("btnPrvRack");
        auto *next = h.page->findChild<QPushButton *>("btnNextRack");
        require(racks && table && add && previous && next, "controls");
        auto current = [&]() -> QWidget *
        {
            require(racks->currentItem(), "current list item");
            require(racks->visualItemRect(racks->currentItem()).top() == 0, "current card at top");
            require(racks->currentItem()->sizeHint().height() == h.page->height(), "expanded current row");
            auto *card = racks->itemWidget(racks->currentItem());
            require(card && card->isVisible(), "visible current card");
            return card;
        };
        auto cardLabel = [&](QWidget *card)
        {
            auto *label = card->findChild<QLabel *>("lblRackId");
            require(label, "rack label");
            return label->text();
        };
        auto addDraft = [&]()
        {
            QMetaObject::invokeMethod(add, "clicked", Qt::DirectConnection);
            require(racks->count() == 2 && racks->row(racks->currentItem()) == 1, "draft row appended");
            auto *card = current();
            require(card->property("localDraft").toBool(), "draft property");
            require(cardLabel(card) == QString::fromUtf8("预设1:S01"), "draft label");
            require(table->rowCount() == 8, "draft eight-hole table");
            auto *holes = card->findChild<QWidget *>("wgtRack");
            require(holes &&
                        holes->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size() == 8,
                    "draft eight-hole card");
            const QImage pixels = holes->grab().toImage();
            require(pixels.pixelColor(holes->width() / 2, 10) == QColor("#B7D7FF"), "draft selected blue");
            require(h.page->snapshot().racks.size() == 1, "draft absent from snapshot");
            require(sql(h.database(), "SELECT COUNT(*) FROM sample_local_rack").toInt() == 1,
                    "draft absent from database");
            return card;
        };
        QWidget *draft = addDraft();
        answer(QMessageBox::Cancel);
        require(!h.page->prepareToLeave(), "cancel discard");
        require(current() == draft && h.page->hasUnsavedChanges(), "cancel retains draft card");
        answer(QMessageBox::Discard);
        require(h.page->prepareToLeave(), "discard draft");
        require(racks->count() == 1 && !current()->property("localDraft").toBool(),
                "discard removes draft card");
        require(cardLabel(current()) == QString::fromUtf8("急诊") && table->rowCount() == 3,
                "discard restores emergency");
        require(h.page->snapshot().racks.size() == 1, "discard leaves snapshot unchanged");

        addDraft();
        h.page->findChild<QPushButton *>("btnMenuSave")->click();
        require(until([&]() { return !h.page->isBusy(); }), "save draft");
        require(h.page->snapshot().racks.size() == 2 && h.page->snapshot().racks.last().id > 0,
                "saved real rack id");
        require(racks->count() == 2 && !current()->property("localDraft").toBool(),
                "real snapshot replaces draft");
        require(table->rowCount() == 8 && !previous->isHidden() && next->isHidden(),
                "ordinary last navigation");
        capture(h.page, "samples_ordinary_restored_visibility");
        previous->click();
        require(cardLabel(current()) == QString::fromUtf8("急诊") && table->rowCount() == 3,
                "previous visible emergency three holes");
        require(previous->isHidden() && !next->isHidden(), "emergency first navigation");
        next->click();
        require(cardLabel(current()) == QString::fromUtf8("预设1:S01") && table->rowCount() == 8,
                "next visible ordinary eight holes");
        h.page->findChild<QPushButton *>("btnMenuReturn")->click();
        require(racks->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded, "overview scroll restored");
        for (int row = 0; row < racks->count(); ++row)
        {
            require(racks->item(row)->sizeHint().height() == 136, "overview compact row");
        }
        require(previous->isHidden() && next->isHidden(), "overview navigation hidden");
        h.page->findChild<QPushButton *>("btnEditRack")->click();
        require(cardLabel(current()) == QString::fromUtf8("预设1:S01"), "reopen correct card");
        require(racks->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff, "editor scroll disabled");
        qInfo().noquote() << QJsonDocument(QJsonObject{{"draft_eight_holes_blue", true},
                                                       {"draft_snapshot_or_db_rows", 0},
                                                       {"cancel_retains_draft", true},
                                                       {"discard_removes_draft", true},
                                                       {"save_replaces_with_real_id", true},
                                                       {"previous_emergency_three_holes", true},
                                                       {"next_ordinary_eight_holes", true},
                                                       {"return_and_reopen", true},
                                                       {"hardware_actions", 0},
                                                       {"passed", true}})
                                 .toJson(QJsonDocument::Compact);
        return 0;
    }
    catch (const std::exception &error)
    {
        qCritical() << error.what();
        return 1;
    }
}
