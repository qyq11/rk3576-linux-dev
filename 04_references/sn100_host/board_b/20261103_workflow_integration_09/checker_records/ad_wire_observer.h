#ifndef SN100_CONNECTED_AD_WIRE_OBSERVER_H
#define SN100_CONNECTED_AD_WIRE_OBSERVER_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QTableWidget>
#include <QVariant>
#include <QVector>
#include <QtGlobal>
#include <array>

// 联机检查专用观察器。只读实际RX和现有表格，不查询主控、不修改界面或账户。
// 独立依据：原commandthread.cpp的06/07、大端长度、分片序号/总数、异或、08/09；
// 原commandreceiver.cpp::pcQueryAD只消费count对、重复键取首值；
// 原frmDriverSet::slotPcQueryAD按完整通道字符串和保留空项的空格拆分显示前四项。
// 本文件不包含或调用正式协议解析类，避免拿同一解析结果自证。
class AdWireObserver
{
public:
    AdWireObserver() { reset(0); }

    // 只能由调用者在确认真正的新connected代次后调用，不能因收到旧RX自动换代。
    void reset(quint64 generation)
    {
        generation_ = generation;
        buffer_.clear();
        clearAssembly();
        errors_.clear();
        errorCount_ = 0;
        rejectedGenerationFeeds_ = 0;
        frames_ = messages_ = validQqCount_ = seenQqCount_ = fragmentedMessages_ = 0;
        currentQqReceived_ = false;
        rawQq_.clear();
        rawQqHistory_ = QJsonArray();
        historyTruncated_ = false;
        latestChannels_.clear();
        ignoredTailFields_ = QJsonArray();
        duplicateChannelKeys_ = 0;
        declaredCount_ = -1;
        expected_.clear();
        for (int channel = 1; channel <= 24; ++channel)
            expected_.insert(QString::number(channel), Cells());
    }

    // 只传设备到上位机的原始RX字节。半帧、一个RX内多帧和跨RX分片均独立累积。
    // 任何坏帧或代次错误会留下错误，直到真正新连接reset；不悄悄拿旧值放行。
    bool feed(const QByteArray &rawRx, quint64 generation)
    {
        if (generation_ == 0 || generation != generation_)
        {
            ++rejectedGenerationFeeds_;
            fail(QStringLiteral("RX代次%1与观察代次%2不一致，未使用这些字节。")
                 .arg(generation).arg(generation_));
            return false;
        }
        if (rawRx.isEmpty()) return errors_.isEmpty();
        // 本检查器的内存预算，不是设备协议最大值声明。
        if (rawRx.size() > 2 * 1024 * 1024 || buffer_.size() > 2 * 1024 * 1024 - rawRx.size())
        {
            fail(QStringLiteral("独立观察器RX缓冲超过2MiB预算。"));
            buffer_.clear();
            return false;
        }
        buffer_.append(rawRx);
        const QByteArray header("\x06\x07", 2);
        while (!buffer_.isEmpty())
        {
            const int start = buffer_.indexOf(header);
            if (start < 0)
            {
                const bool keepStart = byteAt(buffer_, buffer_.size() - 1) == 0x06;
                const int discarded = buffer_.size() - (keepStart ? 1 : 0);
                if (discarded > 0) fail(QStringLiteral("RX存在%1字节无法确认帧头，未作为有效报文。" ).arg(discarded));
                buffer_ = keepStart ? QByteArray(1, char(0x06)) : QByteArray();
                break;
            }
            if (start > 0)
            {
                fail(QStringLiteral("RX帧头前有%1字节，已丢弃但保留错误。" ).arg(start));
                buffer_.remove(0, start);
            }
            if (buffer_.size() < 4) break;
            const int length = (byteAt(buffer_, 2) << 8) | byteAt(buffer_, 3);
            if (length <= 4)
            {
                fail(QStringLiteral("帧数据长度%1不足分片头和非空业务内容。" ).arg(length));
                buffer_.remove(0, 2);
                continue;
            }
            const int frameBytes = length + 7;
            if (buffer_.size() < frameBytes) break;
            if (byteAt(buffer_, length + 5) != 0x08 || byteAt(buffer_, length + 6) != 0x09)
            {
                fail(QStringLiteral("RX长度所指位置没有08/09帧尾。"));
                buffer_.remove(0, 2);
                continue;
            }
            const QByteArray packet = buffer_.mid(4, length);
            int checksum = 0;
            for (char value : packet) checksum ^= static_cast<unsigned char>(value);
            const bool validChecksum = checksum == byteAt(buffer_, length + 4);
            buffer_.remove(0, frameBytes);
            if (!validChecksum)
            {
                fail(QStringLiteral("RX异或校验不一致，未使用该分片。"));
                continue;
            }
            ++frames_;
            acceptPacket(packet);
        }
        return errors_.isEmpty();
    }

    bool currentQqReceived() const { return currentQqReceived_; }
    quint64 generation() const { return generation_; }
    QString error() const { return errors_.join(QStringLiteral("\n")); }
    QByteArray rawQq() const { return rawQq_; }

    QJsonObject report() const
    {
        QJsonObject channels;
        for (auto it = latestChannels_.cbegin(); it != latestChannels_.cend(); ++it)
            channels.insert(it.key(), it.value());
        QJsonObject result;
        result.insert(QStringLiteral("generation"), QString::number(generation_));
        result.insert(QStringLiteral("current_qq_received"), currentQqReceived_);
        result.insert(QStringLiteral("error"), error());
        result.insert(QStringLiteral("error_count"), errorCount_);
        result.insert(QStringLiteral("rejected_generation_feeds"), rejectedGenerationFeeds_);
        result.insert(QStringLiteral("valid_frames"), frames_);
        result.insert(QStringLiteral("complete_messages"), messages_);
        result.insert(QStringLiteral("fragmented_messages"), fragmentedMessages_);
        result.insert(QStringLiteral("seen_qq_count"), seenQqCount_);
        result.insert(QStringLiteral("valid_qq_count"), validQqCount_);
        result.insert(QStringLiteral("raw_qq"), QString::fromLatin1(rawQq_));
        result.insert(QStringLiteral("raw_qq_hex"), QString::fromLatin1(rawQq_.toHex(' ')));
        result.insert(QStringLiteral("raw_qq_history"), rawQqHistory_);
        result.insert(QStringLiteral("history_truncated"), historyTruncated_);
        result.insert(QStringLiteral("declared_count"), declaredCount_);
        result.insert(QStringLiteral("duplicate_channel_keys"), duplicateChannelKeys_);
        result.insert(QStringLiteral("channels_first_declared_pairs"), channels);
        result.insert(QStringLiteral("ignored_tail_fields"), ignoredTailFields_);
        result.insert(QStringLiteral("buffered_bytes"), buffer_.size());
        result.insert(QStringLiteral("assembly_bytes"), assembly_.size());
        result.insert(QStringLiteral("next_fragment"), nextFragment_);
        result.insert(QStringLiteral("total_fragments"), totalFragments_);
        return result;
    }

    // 调用者须先等当前代Qq到达，再让GUI事件处理完成；本方法从不processEvents。
    // 必须存在完整字符串键"1".."24"各一行，行号/顺序可任意；"01"不会被当作"1"。
    QJsonObject compare(const QTableWidget *table) const
    {
        QJsonObject result = comparisonBase(QStringLiteral("qq_text_and_reported"));
        if (!errors_.isEmpty()) return finish(result, QStringLiteral("fail"), QJsonArray(), 0, 0, error());
        if (!table) return finish(result, QStringLiteral("pending"), QJsonArray(), 0, 0, QStringLiteral("通道表尚不存在。"));
        if (generation_ == 0 || !currentQqReceived_)
            return finish(result, QStringLiteral("pending"), QJsonArray(), 0, 0, QStringLiteral("尚未收到本代有效Qq，不能借用旧连接快照。"));
        if (!buffer_.isEmpty() || totalFragments_ != 0)
            return finish(result, QStringLiteral("pending"), QJsonArray(), 0, 0, QStringLiteral("RX尚有半帧或未完成分片，先等待完整报文。"));
        if (table->columnCount() < 6)
            return finish(result, QStringLiteral("fail"), QJsonArray(), 0, 0, QStringLiteral("通道表不足六列。"));
        QMap<QString, QVector<int>> rows;
        for (int row = 0; row < table->rowCount(); ++row)
            if (const auto *number = table->item(row, 0)) rows[number->text()].append(row);
        QJsonArray mismatches;
        int checkedRows = 0, checkedCells = 0;
        for (int channel = 1; channel <= 24; ++channel)
        {
            const QString key = QString::number(channel);
            const QVector<int> matches = rows.value(key);
            if (matches.size() != 1)
            {
                QJsonObject mismatch;
                mismatch.insert(QStringLiteral("channel"), key);
                mismatch.insert(QStringLiteral("reason"), matches.isEmpty() ? QStringLiteral("missing_exact_row_key") : QStringLiteral("duplicate_exact_row_key"));
                mismatch.insert(QStringLiteral("matching_rows"), matches.size());
                mismatches.append(mismatch);
                continue;
            }
            ++checkedRows;
            const Cells expected = expected_.value(key);
            for (int valueColumn = 0; valueColumn < 4; ++valueColumn)
            {
                ++checkedCells;
                const auto *item = table->item(matches.first(), valueColumn + 2);
                appendCellMismatch(mismatches, item, key, matches.first(), valueColumn + 2,
                                   expected.at(valueColumn).text, expected.at(valueColumn).reported);
            }
        }
        return finish(result, mismatches.isEmpty() ? QStringLiteral("pass") : QStringLiteral("fail"),
                      mismatches, checkedRows, checkedCells, QString());
    }

    // 断开/换代后核对当前表实际存在的全部数值格。无需Base/HIL行，但有则也须清空。
    QJsonObject compareCleared(const QTableWidget *table) const
    {
        QJsonObject result = comparisonBase(QStringLiteral("all_value_cells_cleared"));
        if (!table) return finish(result, QStringLiteral("pending"), QJsonArray(), 0, 0, QStringLiteral("通道表尚不存在。"));
        if (table->columnCount() < 6 || table->rowCount() == 0)
            return finish(result, QStringLiteral("pending"), QJsonArray(), 0, 0, QStringLiteral("通道行或六列尚未建立，不能把空表当作清除通过。"));
        QJsonArray mismatches;
        int checkedCells = 0;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            const auto *number = table->item(row, 0);
            const QString key = number ? number->text() : QStringLiteral("<missing-row-key>");
            for (int column = 2; column <= 5; ++column)
            {
                ++checkedCells;
                appendCellMismatch(mismatches, table->item(row, column), key, row, column, QStringLiteral("-"), false);
            }
        }
        return finish(result, mismatches.isEmpty() && errors_.isEmpty() ? QStringLiteral("pass") : QStringLiteral("fail"),
                      mismatches, table->rowCount(), checkedCells, error());
    }

private:
    struct ExpectedCell
    {
        QString text = QStringLiteral("-");
        bool reported = false;
    };
    using Cells = std::array<ExpectedCell, 4>;

    quint64 generation_ = 0;
    QByteArray buffer_, assembly_, rawQq_;
    int nextFragment_ = 0, totalFragments_ = 0;
    QStringList errors_;
    int errorCount_ = 0, rejectedGenerationFeeds_ = 0;
    int frames_ = 0, messages_ = 0, seenQqCount_ = 0, validQqCount_ = 0, fragmentedMessages_ = 0;
    bool currentQqReceived_ = false, historyTruncated_ = false;
    QJsonArray rawQqHistory_, ignoredTailFields_;
    int declaredCount_ = -1, duplicateChannelKeys_ = 0;
    QMap<QString, QString> latestChannels_;
    QMap<QString, Cells> expected_;

    static int byteAt(const QByteArray &bytes, int index)
    {
        return static_cast<unsigned char>(bytes.at(index));
    }

    void clearAssembly()
    {
        assembly_.clear();
        nextFragment_ = totalFragments_ = 0;
    }

    void fail(const QString &message)
    {
        ++errorCount_;
        if (errors_.size() < 32) errors_.append(message);
        clearAssembly();
    }

    void acceptPacket(const QByteArray &packet)
    {
        const int index = (byteAt(packet, 0) << 8) | byteAt(packet, 1);
        const int total = (byteAt(packet, 2) << 8) | byteAt(packet, 3);
        if (total == 0 || index >= total)
        {
            fail(QStringLiteral("分片序号%1/总数%2无效。" ).arg(index).arg(total));
            return;
        }
        if (index == 0)
        {
            if (totalFragments_ != 0)
                fail(QStringLiteral("旧消息尚未组完就收到新起始片，不能把两条消息拼接。"));
            totalFragments_ = total;
            nextFragment_ = 0;
        }
        if (totalFragments_ == 0 || total != totalFragments_ || index != nextFragment_)
        {
            fail(QStringLiteral("分片顺序不连贯：期待%1/%2，实际%3/%4。")
                 .arg(nextFragment_).arg(totalFragments_).arg(index).arg(total));
            return;
        }
        const QByteArray fragment = packet.mid(4);
        if (assembly_.size() > 4 * 1024 * 1024 - fragment.size())
        {
            fail(QStringLiteral("独立观察器单条业务消息超过4MiB预算。"));
            return;
        }
        assembly_.append(fragment);
        ++nextFragment_;
        if (nextFragment_ == totalFragments_)
        {
            const QByteArray message = assembly_;
            if (totalFragments_ > 1) ++fragmentedMessages_;
            clearAssembly();
            ++messages_;
            if (message.startsWith("Qq")) acceptQq(message);
        }
    }

    void acceptQq(const QByteArray &message)
    {
        ++seenQqCount_;
        rawQq_ = message;
        if (rawQqHistory_.size() == 32)
        {
            rawQqHistory_.removeAt(0);
            historyTruncated_ = true;
        }
        rawQqHistory_.append(QString::fromLatin1(message));
        QStringList fields;
        int cursor = 2;
        while (cursor < message.size())
        {
            if (message.at(cursor++) != '[')
            {
                fail(QStringLiteral("Qq字段未连续放在完整方括号内。"));
                return;
            }
            const int start = cursor;
            while (cursor < message.size() && message.at(cursor) != ']')
            {
                const int character = byteAt(message, cursor++);
                if (character < 0x20 || character > 0x7e || character == '[')
                {
                    fail(QStringLiteral("Qq含无法独立确认的非ASCII/控制字符或嵌套括号。"));
                    return;
                }
            }
            if (cursor == message.size() || fields.size() >= 8192)
            {
                fail(QStringLiteral("Qq字段未闭合或超过观察器8192字段预算。"));
                return;
            }
            fields.append(QString::fromLatin1(message.constData() + start, cursor - start));
            ++cursor;
        }
        bool countOk = false;
        const int count = fields.value(0).toInt(&countOk);
        if (!countOk || count < 0 || count > 4095 || fields.size() < 1 + count * 2)
        {
            fail(QStringLiteral("Qq声明数量无法确认，或声明的前N对字段不足。"));
            return;
        }
        QMap<QString, QString> channels;
        int duplicates = 0;
        for (int index = 0; index < count; ++index)
        {
            const QString key = fields.at(1 + index * 2);
            if (channels.contains(key)) ++duplicates;
            else channels.insert(key, fields.at(2 + index * 2));
        }
        QJsonArray tail;
        for (int index = 1 + count * 2; index < fields.size(); ++index) tail.append(fields.at(index));
        // 缺通道整行置'-'。已有通道短列表按原版保留旧文字，但本次reported=false。
        for (auto it = expected_.begin(); it != expected_.end(); ++it)
            if (!channels.contains(it.key())) it.value() = Cells();
        for (auto it = channels.cbegin(); it != channels.cend(); ++it)
        {
            const QStringList parts = it.value().split(QLatin1Char(' '), Qt::KeepEmptyParts);
            Cells &cells = expected_[it.key()];
            for (int column = 0; column < 4; ++column)
            {
                cells[column].reported = column < parts.size();
                if (cells[column].reported) cells[column].text = parts.at(column);
            }
        }
        latestChannels_ = channels;
        ignoredTailFields_ = tail;
        duplicateChannelKeys_ = duplicates;
        declaredCount_ = count;
        ++validQqCount_;
        currentQqReceived_ = true;
    }

    QJsonObject comparisonBase(const QString &kind) const
    {
        QJsonObject result;
        result.insert(QStringLiteral("kind"), kind);
        result.insert(QStringLiteral("generation"), QString::number(generation_));
        result.insert(QStringLiteral("current_qq_received"), currentQqReceived_);
        result.insert(QStringLiteral("valid_qq_count"), validQqCount_);
        result.insert(QStringLiteral("physical_value_validity_checked"), false);
        return result;
    }

    static QJsonObject finish(QJsonObject result, const QString &status, const QJsonArray &mismatches,
                              int rows, int cells, const QString &reason)
    {
        result.insert(QStringLiteral("status"), status);
        result.insert(QStringLiteral("ok"), status == QStringLiteral("pass"));
        result.insert(QStringLiteral("reason"), reason);
        result.insert(QStringLiteral("checked_rows"), rows);
        result.insert(QStringLiteral("checked_cells"), cells);
        result.insert(QStringLiteral("mismatches"), mismatches);
        return result;
    }

    static void appendCellMismatch(QJsonArray &mismatches, const QTableWidgetItem *item,
            const QString &channel, int row, int column, const QString &expectedText, bool expectedReported)
    {
        const QVariant actualReported = item ? item->data(Qt::UserRole) : QVariant();
        const bool validFlag = actualReported.isValid() && actualReported.type() == QVariant::Bool;
        if (item && item->text() == expectedText && validFlag && actualReported.toBool() == expectedReported) return;
        QJsonObject mismatch;
        mismatch.insert(QStringLiteral("channel"), channel);
        mismatch.insert(QStringLiteral("row"), row);
        mismatch.insert(QStringLiteral("column"), column);
        mismatch.insert(QStringLiteral("cell_exists"), item != nullptr);
        mismatch.insert(QStringLiteral("expected_text"), expectedText);
        mismatch.insert(QStringLiteral("actual_text"), item ? item->text() : QString());
        mismatch.insert(QStringLiteral("expected_reported"), expectedReported);
        mismatch.insert(QStringLiteral("actual_reported"), actualReported.toBool());
        mismatch.insert(QStringLiteral("reported_is_explicit_bool"), validFlag);
        mismatches.append(mismatch);
    }
};

#endif
