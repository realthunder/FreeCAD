/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_MESSAGECOLLAPSE_H
#define GUI_MESSAGECOLLAPSE_H

#include <cstddef>
#include <QChar>
#include <QString>
#include <QStringList>

namespace Gui
{

/** Key two messages are "the same message" by, for collapsing repeats on screen.
 *
 * Digits are skipped rather than compared, because the messages that arrive in
 * floods are the same sentence carrying a different number - a source line, an
 * element index, a coordinate - and a reader gains nothing from seeing each
 * variant.
 *
 * The judgement is made on the message's first @a keyLength characters, and it
 * is those characters that are counted, not the ones kept: a message dense in
 * identifiers would otherwise be judged on a far longer stretch of itself than
 * one made of words, which is precisely backwards. Five FreeCAD warnings that
 * shared their first 101 characters keyed apart because skipping their digits
 * carried the window on into the element hashes behind them - and a hash is
 * mostly letters, so nothing was skipped there at all.
 *
 * FNV-1a. A collision costs one wrongly collapsed line and nothing else, so 64
 * bits is far more than this needs.
 */
inline std::size_t messageCollapseKey(const QString& text, int keyLength)
{
    std::size_t hash = 1469598103934665603ULL;
    const int end = keyLength < text.size() ? keyLength : static_cast<int>(text.size());
    for (int i = 0; i < end; ++i) {
        const QChar chr = text.at(i);
        if (chr.isDigit()) {
            continue;
        }
        hash = (hash ^ chr.unicode()) * 1099511628211ULL;
    }
    return hash;
}

/** Ceiling on the messages kept behind one fold.
 *
 * A storm is unbounded and these are held in memory, so the repeat count keeps
 * rising after the buffer stops growing and an opened fold says less than the
 * count promised.
 */
inline constexpr int messageFoldLimit = 200;

/** The branch a message revealed by opening a fold leads with.
 *
 * It marks the line as belonging to the one above it rather than to the log,
 * which matters most where the two look alike: the messages behind a fold are
 * near-copies of the line they were folded into.
 */
inline QString messageFoldBranch(int index, int count)
{
    //spelled out rather than written literally: the source encoding a compiler
    //assumes for a non-ASCII byte is not something this has to depend on
    return index + 1 < count ? QStringLiteral("\u251C\u2500 ")   // vertical and right
                             : QStringLiteral("\u2514\u2500 ");  // up and right
}

/** The messages one shown line stands in for.
 *
 * The Report view and the notification area each decide for themselves *when* a
 * message is folded away - one holds a line back on a timer, the other merges on
 * arrival - but what a fold *is* is the same in both, and was written twice
 * before it was written here: a key, the first message held, the messages behind
 * it up to a cap, and a count that keeps rising after the cap stops the buffer.
 * Getting that wrong in one place and right in the other is the failure this
 * exists to prevent.
 */
class MessageFold
{
public:
    MessageFold() = default;
    explicit MessageFold(std::size_t messageKey)
        : foldKey(messageKey)
    {}

    /// what this fold judges "the same message" by
    std::size_t key() const
    {
        return foldKey;
    }

    /// how many messages have been folded away behind the line, which is what
    /// the line's count reports and is one less than the messages it speaks for
    int count() const
    {
        return folded;
    }

    bool isEmpty() const
    {
        return folded == 0;
    }

    /// the first message folded in, which is the one shown in place of the rest
    ///
    /// They differ only where the key ignored a difference, so the first is as
    /// good a witness as any and is the one that arrived soonest.
    const QString& exemplar() const
    {
        return first;
    }

    /// the held messages, as they are to be shown when the fold is opened
    const QStringList& held() const
    {
        return texts;
    }

    /** Fold one more message in.
     *
     * @param text the message itself, kept if it is the first
     * @param display how it should read when the fold is opened - the Report view
     * stamps it with the time it arrived, which is the one thing a reader opens a
     * fold to establish and cannot recover afterwards
     */
    void add(const QString& text, const QString& display)
    {
        ++folded;
        if (first.isEmpty()) {
            first = text;
        }
        //a storm is unbounded and these are held in memory: the buffer stops
        //growing while the count carries on, so an opened fold says less than the
        //line promised rather than the process paying for a fold nobody opens
        if (texts.size() < messageFoldLimit) {
            texts.append(display);
        }
    }

    void add(const QString& text)
    {
        add(text, text);
    }

    /// forget what is held, keeping the key: the line has been shown
    void clear()
    {
        folded = 0;
        first.clear();
        texts.clear();
    }

private:
    std::size_t foldKey = 0;
    int folded = 0;
    QString first;
    QStringList texts;
};

}  // namespace Gui

#endif  // GUI_MESSAGECOLLAPSE_H
