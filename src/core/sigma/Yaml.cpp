#include "Yaml.h"

namespace lh::yaml {

namespace {

struct Row
{
    int indentation = 0;
    QString text;
    int number = 0;
    bool empty = false;
};

bool isSpaceChar(QChar c)
{
    return c == u' ' || c == u'\t';
}

QString withoutComment(const QString& text)
{
    bool simple = false;
    bool double_ = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (simple) {
            if (c == u'\'') {
                if (i + 1 < text.size() && text[i + 1] == u'\'')
                    ++i;
                else
                    simple = false;
            }
        } else if (double_) {
            if (c == u'\\')
                ++i;
            else if (c == u'"')
                double_ = false;
        } else if (c == u'\'' && (i == 0 || isSpaceChar(text[i - 1]) || text[i - 1] == u'[' || text[i - 1] == u','
                                  || text[i - 1] == u':' || text[i - 1] == u'-' || text[i - 1] == u'{')) {
            simple = true;
        } else if (c == u'"' && (i == 0 || isSpaceChar(text[i - 1]) || text[i - 1] == u'[' || text[i - 1] == u','
                                 || text[i - 1] == u':' || text[i - 1] == u'-' || text[i - 1] == u'{')) {
            double_ = true;
        } else if (c == u'#' && (i == 0 || isSpaceChar(text[i - 1]))) {
            QString result = text.left(i);
            while (!result.isEmpty() && isSpaceChar(result.back()))
                result.chop(1);
            return result;
        }
    }
    QString result = text;
    while (!result.isEmpty() && isSpaceChar(result.back()))
        result.chop(1);
    return result;
}

bool isListItem(const QString& text)
{
    return text == QLatin1String("-") || text.startsWith(QLatin1String("- "))
           || text.startsWith(QLatin1String("-\t"));
}

qsizetype quoteEnd(const QString& text, qsizetype start)
{
    const QChar quote = text[start];
    for (qsizetype i = start + 1; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quote == u'\'') {
            if (c == u'\'') {
                if (i + 1 < text.size() && text[i + 1] == u'\'')
                    ++i;
                else
                    return i;
            }
        } else if (c == u'\\') {
            ++i;
        } else if (c == u'"') {
            return i;
        }
    }
    return -1;
}

QString decodeQuoted(const QString& text, qsizetype start, qsizetype end)
{
    const QChar quote = text[start];
    QString result;
    result.reserve(end - start);
    for (qsizetype i = start + 1; i < end; ++i) {
        const QChar c = text[i];
        if (quote == u'\'') {
            result += c;
            if (c == u'\'')
                ++i;
            continue;
        }
        if (c != u'\\' || i + 1 >= end) {
            result += c;
            continue;
        }
        const QChar next = text[++i];
        switch (next.unicode()) {
        case u'n':
            result += u'\n';
            break;
        case u't':
            result += u'\t';
            break;
        case u'r':
            result += u'\r';
            break;
        case u'0':
            result += QChar(u'\0');
            break;
        case u'x':
        case u'u':
        case u'U': {
            const int length = next == u'x' ? 2 : next == u'u' ? 4 : 8;
            bool ok = false;
            const uint code = text.mid(i + 1, length).toUInt(&ok, 16);
            if (ok) {
                result += QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1);
                i += length;
            } else {
                result += next;
            }
            break;
        }
        default:
            result += next;
        }
    }
    return result;
}

Node scalar(const QString& value, bool quoted)
{
    Node node;
    const bool null = value.isEmpty() || value == QLatin1String("~")
                      || value.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0;
    if (!quoted && null)
        return node;
    node.type = Node::Type::Scalar;
    node.value = value;
    node.quoted = quoted;
    return node;
}

QString unanchored(QString text)
{
    for (;;) {
        if (text.startsWith(QLatin1Char('&')) || text.startsWith(QLatin1Char('!'))) {
            qsizetype space = 0;
            while (space < text.size() && !isSpaceChar(text[space]))
                ++space;
            text = text.mid(space).trimmed();
            continue;
        }
        return text;
    }
}

class Reader
{
public:
    explicit Reader(const QString& source)
    {
        const QStringList lines = source.split(QLatin1Char('\n'));
        bool content = false;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            QString raw = lines[i];
            if (raw.endsWith(QLatin1Char('\r')))
                raw.chop(1);
            if (i == 0 && raw.startsWith(QChar(0xfeff)))
                raw.remove(0, 1);
            const QString bare = raw.trimmed();
            if (bare.startsWith(QLatin1String("%YAML")) || bare.startsWith(QLatin1String("%TAG")))
                continue;
            const bool splitter = raw == QLatin1String("---") || raw.startsWith(QLatin1String("--- "))
                                  || raw == QLatin1String("...");
            if (splitter) {
                if (content)
                    break;
                continue;
            }
            Row line;
            while (line.indentation < raw.size() && raw[line.indentation] == u' ')
                ++line.indentation;
            line.text = raw.mid(line.indentation);
            line.number = static_cast<int>(i) + 1;
            line.empty = bare.isEmpty() || bare.startsWith(QLatin1Char('#'));
            content = content || !line.empty;
            m_lines.push_back(std::move(line));
        }
    }

    Node document()
    {
        Node root = block(0);
        skipEmpty();
        if (!end())
            throw Error(qtTrId("rules.error_indentation"), m_lines[m_position].number);
        return root;
    }

private:
    [[nodiscard]] bool end() const { return m_position >= m_lines.size(); }
    [[nodiscard]] int number() const
    {
        if (!end())
            return m_lines[m_position].number;
        return m_lines.empty() ? 0 : m_lines.back().number;
    }

    void skipEmpty()
    {
        while (!end() && m_lines[m_position].empty)
            ++m_position;
    }

    static bool splitKey(const QString& text, QString& key, QString& rest)
    {
        if (text.isEmpty() || text[0] == u'[' || text[0] == u'{' || isListItem(text))
            return false;
        qsizetype position = 0;
        if (text[0] == u'"' || text[0] == u'\'') {
            const qsizetype closing = quoteEnd(text, 0);
            if (closing < 0)
                return false;
            position = closing + 1;
            while (position < text.size() && isSpaceChar(text[position]))
                ++position;
            if (position >= text.size() || text[position] != u':')
                return false;
            if (position + 1 < text.size() && !isSpaceChar(text[position + 1]))
                return false;
            key = decodeQuoted(text, 0, closing);
            rest = text.mid(position + 1);
            return true;
        }
        for (; position < text.size(); ++position) {
            if (text[position] != u':')
                continue;
            if (position + 1 == text.size() || isSpaceChar(text[position + 1])) {
                key = text.left(position).trimmed();
                rest = text.mid(position + 1);
                return !key.isEmpty();
            }
        }
        return false;
    }

    Node block(int minIndentation)
    {
        skipEmpty();
        if (end())
            return {};
        const Row& line = m_lines[m_position];
        if (line.indentation < minIndentation)
            return {};
        const QString text = withoutComment(line.text);
        if (isListItem(text))
            return list(line.indentation);
        QString key;
        QString rest;
        if (splitKey(text, key, rest))
            return table(line.indentation);
        ++m_position;
        return inlineValue(text.trimmed(), line.indentation - 1);
    }

    Node list(int indentation)
    {
        Node node;
        node.type = Node::Type::List;
        for (;;) {
            skipEmpty();
            if (end())
                break;
            Row& line = m_lines[m_position];
            if (line.indentation != indentation)
                break;
            const QString text = withoutComment(line.text);
            if (!isListItem(text))
                break;
            int shift = 1;
            while (shift < line.text.size() && isSpaceChar(line.text[shift]))
                ++shift;
            const QString rest = text.mid(1).trimmed();
            if (rest.isEmpty()) {
                ++m_position;
                node.elements.push_back(block(indentation + 1));
                continue;
            }
            QString key;
            QString value;
            if (isListItem(rest) || splitKey(rest, key, value)) {
                line.text = line.text.mid(shift);
                line.indentation = indentation + shift;
                node.elements.push_back(block(line.indentation));
                continue;
            }
            ++m_position;
            node.elements.push_back(inlineValue(rest, indentation));
        }
        return node;
    }

    Node table(int indentation)
    {
        Node node;
        node.type = Node::Type::Table;
        for (;;) {
            skipEmpty();
            if (end())
                break;
            const Row& line = m_lines[m_position];
            if (line.indentation != indentation)
                break;
            const QString text = withoutComment(line.text);
            if (isListItem(text))
                break;
            QString key;
            QString rest;
            if (!splitKey(text, key, rest))
                throw Error(qtTrId("rules.error_key"), line.number);
            ++m_position;
            node.keys.push_back(key);
            node.elements.push_back(valueAfterKey(unanchored(rest.trimmed()), indentation));
        }
        return node;
    }

    Node valueAfterKey(const QString& rest, int indentation)
    {
        if (rest.isEmpty()) {
            skipEmpty();
            if (end())
                return {};
            const Row& next = m_lines[m_position];
            if (next.indentation > indentation)
                return block(next.indentation);
            if (next.indentation == indentation && isListItem(withoutComment(next.text)))
                return list(indentation);
            return {};
        }
        if (rest[0] == u'|' || rest[0] == u'>')
            return literalBlock(rest, indentation);
        return inlineValue(rest, indentation);
    }

    Node inlineValue(QString text, int parentIndentation)
    {
        text = unanchored(text);
        if (text.isEmpty())
            return {};
        if (text[0] == u'[' || text[0] == u'{') {
            while (!balancedStream(text) && !end()) {
                text += QLatin1Char(' ') + withoutComment(m_lines[m_position].text).trimmed();
                ++m_position;
            }
            qsizetype position = 0;
            Node node = stream(text, position);
            return node;
        }
        if (text[0] == u'"' || text[0] == u'\'') {
            while (quoteEnd(text, 0) < 0 && !end()) {
                const Row& line = m_lines[m_position];
                text += QLatin1Char(' ') + line.text.trimmed();
                ++m_position;
            }
            const qsizetype closing = quoteEnd(text, 0);
            if (closing < 0)
                throw Error(qtTrId("rules.error_quote"), number());
            return scalar(decodeQuoted(text, 0, closing), true);
        }
        while (!end()) {
            const Row& line = m_lines[m_position];
            if (line.empty || line.indentation <= parentIndentation)
                break;
            const QString remainder = withoutComment(line.text);
            QString key;
            QString rest;
            if (isListItem(remainder) || splitKey(remainder, key, rest))
                break;
            text += QLatin1Char(' ') + remainder.trimmed();
            ++m_position;
        }
        return scalar(text.trimmed(), false);
    }

    Node literalBlock(const QString& indicator, int indentation)
    {
        const bool folded = indicator[0] == u'>';
        const bool keep = indicator.contains(QLatin1Char('+'));
        const bool remove = indicator.contains(QLatin1Char('-'));
        QStringList lines;
        int blockIndentation = -1;
        while (!end()) {
            const Row& line = m_lines[m_position];
            const bool blank = line.text.trimmed().isEmpty();
            if (!blank && line.indentation <= indentation)
                break;
            if (!blank && blockIndentation < 0)
                blockIndentation = line.indentation;
            const int indent = std::max(0, line.indentation - blockIndentation);
            lines << (blank ? QString() : QString(indent, u' ') + line.text);
            ++m_position;
        }
        int trailing = 0;
        while (!lines.isEmpty() && lines.back().isEmpty()) {
            lines.removeLast();
            ++trailing;
        }
        QString text;
        if (folded) {
            for (qsizetype i = 0; i < lines.size(); ++i) {
                if (i > 0)
                    text += (lines[i].isEmpty() || lines[i - 1].isEmpty()) ? QStringLiteral("\n")
                                                                            : QStringLiteral(" ");
                text += lines[i];
            }
        } else {
            text = lines.join(QLatin1Char('\n'));
        }
        if (!text.isEmpty() && !remove)
            text += QLatin1Char('\n');
        if (keep)
            text += QString(trailing, u'\n');
        return scalar(text, true);
    }

    static bool balancedStream(const QString& text)
    {
        int depth = 0;
        for (qsizetype i = 0; i < text.size(); ++i) {
            const QChar c = text[i];
            if (c == u'"' || c == u'\'') {
                const qsizetype closing = quoteEnd(text, i);
                if (closing < 0)
                    return false;
                i = closing;
            } else if (c == u'[' || c == u'{') {
                ++depth;
            } else if (c == u']' || c == u'}') {
                --depth;
            }
        }
        return depth <= 0;
    }

    Node stream(const QString& text, qsizetype& position)
    {
        auto spaces = [&] {
            while (position < text.size() && isSpaceChar(text[position]))
                ++position;
        };
        spaces();
        if (position >= text.size())
            return {};
        const QChar c = text[position];
        if (c == u'[' || c == u'{') {
            const bool table = c == u'{';
            const QChar closing = table ? u'}' : u']';
            Node node;
            node.type = table ? Node::Type::Table : Node::Type::List;
            ++position;
            for (;;) {
                spaces();
                if (position >= text.size())
                    throw Error(qtTrId("rules.error_collection"), number());
                if (text[position] == closing) {
                    ++position;
                    break;
                }
                if (table) {
                    const qsizetype colon = text.indexOf(QLatin1Char(':'), position);
                    if (colon < 0)
                        throw Error(qtTrId("rules.error_key"), number());
                    QString key = text.mid(position, colon - position).trimmed();
                    if (key.size() >= 2 && (key[0] == u'"' || key[0] == u'\''))
                        key = decodeQuoted(key, 0, key.size() - 1);
                    position = colon + 1;
                    node.keys.push_back(key);
                }
                node.elements.push_back(stream(text, position));
                spaces();
                if (position < text.size() && text[position] == u',')
                    ++position;
            }
            return node;
        }
        if (c == u'"' || c == u'\'') {
            const qsizetype end = quoteEnd(text, position);
            if (end < 0)
                throw Error(qtTrId("rules.error_quote"), number());
            Node node = scalar(decodeQuoted(text, position, end), true);
            position = end + 1;
            return node;
        }
        const qsizetype start = position;
        while (position < text.size() && text[position] != u',' && text[position] != u']' && text[position] != u'}')
            ++position;
        return scalar(text.mid(start, position - start).trimmed(), false);
    }

    std::vector<Row> m_lines;
    size_t m_position = 0;
};

} // namespace

Error::Error(const QString& message, int line)
    : std::runtime_error(message.toStdString()), m_message(message), m_line(line)
{
}

const Node* Node::field(QStringView key) const
{
    if (type != Type::Table)
        return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
            return &elements[i];
    return nullptr;
}

QString Node::text(QStringView key) const
{
    const Node* node = field(key);
    return node && node->type == Type::Scalar ? node->value : QString();
}

QStringList Node::texts() const
{
    if (type == Type::Scalar)
        return {value};
    QStringList list;
    if (type == Type::List)
        for (const Node& element : elements)
            if (element.type == Type::Scalar)
                list << element.value;
    return list;
}

Node parse(const QString& source)
{
    return Reader(source).document();
}

} // namespace lh::yaml
