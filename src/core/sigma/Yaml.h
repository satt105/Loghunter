// Reader for the subset of YAML that Sigma rules need.
#pragma once

#include <QString>
#include <QStringList>

#include <stdexcept>
#include <vector>

namespace lh::yaml {

class Error : public std::runtime_error
{
public:
    Error(const QString& message, int line);
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] int line() const noexcept { return m_line; }

private:
    QString m_message;
    int m_line;
};

struct Node
{
    enum class Type { Null, Scalar, List, Table };

    Type type = Type::Null;
    QString value;
    bool quoted = false;
    std::vector<Node> elements;
    std::vector<QString> keys;

    [[nodiscard]] bool isNull() const noexcept { return type == Type::Null; }
    [[nodiscard]] bool isScalar() const noexcept { return type == Type::Scalar; }
    [[nodiscard]] bool isList() const noexcept { return type == Type::List; }
    [[nodiscard]] bool isTable() const noexcept { return type == Type::Table; }

    [[nodiscard]] const Node* field(QStringView key) const;
    [[nodiscard]] QString text(QStringView key) const;
    [[nodiscard]] QStringList texts() const;
};

Node parse(const QString& source);

} // namespace lh::yaml
