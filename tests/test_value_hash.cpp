#include "lumiere/interpreter/runtime/value.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

namespace lumiere
{
namespace
{

Value fixed_list(std::vector<Value> elements)
{
    auto data = std::make_shared<ListeFixeData>();
    data->elements = std::move(elements);
    return Value::liste_fixe(std::move(data));
}

Value empty_list()
{
    return Value::liste(std::make_shared<ListeData>());
}

/** Every value a key can take, with equal values appearing more than once. */
std::vector<Value> corpus()
{
    const Value shared_list = empty_list();
    return {
        Value::rien(),
        Value::rien(),
        Value::entier(0),
        Value::entier(1),
        Value::entier(1),
        Value::entier(-1),
        Value::decimal(0.0),
        Value::decimal(-0.0),
        Value::decimal(1.0),
        Value::logique(true),
        Value::logique(false),
        Value::symbole(U'é'),
        Value::symbole(U'é'),
        Value::texte(""),
        Value::texte("clé"),
        Value::texte("clé"),
        fixed_list({}),
        fixed_list({Value::entier(1), Value::texte("a")}),
        fixed_list({Value::entier(1), Value::texte("a")}),
        fixed_list({fixed_list({Value::entier(1)})}),
        fixed_list({fixed_list({Value::entier(1)})}),
        shared_list,
        shared_list,
        empty_list(),
        Value::resultat(true, Value::entier(7), std::nullopt),
        Value::resultat(true, Value::entier(7), std::nullopt),
        Value::resultat(false, Value::entier(7), std::nullopt),
    };
}

} // namespace

TEST(ValueHash, EqualValuesHashEqually)
{
    const std::vector<Value> values = corpus();
    for (const Value &left : values)
    {
        for (const Value &right : values)
        {
            if (left == right)
            {
                EXPECT_EQ(value_hash(left), value_hash(right))
                    << "equal values disagree on hash: " << left.to_string();
            }
        }
    }
}

TEST(ValueHash, SignedZerosAreOneKeyAndNumericTypesAreTwo)
{
    EXPECT_TRUE(Value::decimal(0.0) == Value::decimal(-0.0));
    EXPECT_EQ(value_hash(Value::decimal(0.0)), value_hash(Value::decimal(-0.0)));
    // Entier and Decimal never compare equal, so 1 and 1.0 stay distinct keys.
    EXPECT_FALSE(Value::entier(1) == Value::decimal(1.0));
}

TEST(ValueHash, ListsAreKeyedByIdentityAndFixedListsByContent)
{
    const Value first = empty_list();
    const Value second = empty_list();
    EXPECT_FALSE(first == second);
    EXPECT_TRUE(first == first);

    EXPECT_TRUE(fixed_list({Value::entier(1)}) == fixed_list({Value::entier(1)}));
    EXPECT_EQ(value_hash(fixed_list({Value::entier(1)})), value_hash(fixed_list({Value::entier(1)})));
    EXPECT_FALSE(fixed_list({Value::entier(1)}) == fixed_list({Value::entier(2)}));
}

TEST(Dictionary, RefusesANonNumberAsKey)
{
    // Not reachable from source any more, since text conversion rejects "nan",
    // but the runtime must still refuse a key that is not equal to itself.
    const Value absent = Value::decimal(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(absent == absent);
    EXPECT_TRUE(dictionary_key_rejection(absent).has_value());
    EXPECT_FALSE(dictionary_key_rejection(Value::decimal(0.0)).has_value());
    EXPECT_FALSE(dictionary_key_rejection(Value::texte("clé")).has_value());
}

TEST(Dictionary, FindsEveryKeyAcrossIndexGrowthAndRemoval)
{
    DictData dictionary;
    constexpr std::size_t kCount = 500;

    for (std::size_t i = 0; i < kCount; ++i)
    {
        EXPECT_TRUE(dictionary.set(Value::texte("clé" + std::to_string(i)), Value::entier(static_cast<std::int64_t>(i))));
    }
    EXPECT_EQ(dictionary.size(), kCount);

    // Reassigning never adds an entry and never moves one.
    EXPECT_FALSE(dictionary.set(Value::texte("clé0"), Value::entier(-1)));
    EXPECT_EQ(dictionary.size(), kCount);
    EXPECT_EQ(dictionary.items().front().first, Value::texte("clé0"));
    EXPECT_EQ(dictionary.find(Value::texte("clé0"))->second, Value::entier(-1));

    // Removal shifts later positions, so the index must be rebuilt behind it.
    for (std::size_t i = 0; i < kCount; i += 2)
    {
        Value removed;
        EXPECT_TRUE(dictionary.erase(Value::texte("clé" + std::to_string(i)), removed));
    }
    EXPECT_EQ(dictionary.size(), kCount / 2);

    for (std::size_t i = 0; i < kCount; ++i)
    {
        const Value key = Value::texte("clé" + std::to_string(i));
        if (i % 2 == 0)
        {
            EXPECT_EQ(dictionary.find(key), nullptr) << i;
        }
        else
        {
            ASSERT_NE(dictionary.find(key), nullptr) << i;
            EXPECT_EQ(dictionary.find(key)->second, Value::entier(static_cast<std::int64_t>(i)));
        }
    }

    Value ignored;
    EXPECT_FALSE(dictionary.erase(Value::texte("absente"), ignored));
}

} // namespace lumiere
