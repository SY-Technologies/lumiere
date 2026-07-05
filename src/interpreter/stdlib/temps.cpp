#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <chrono>
#include <cctype>
#include <sstream>
#include <thread>

namespace lumiere
{

namespace
{

using TimePointMs = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

// Temps uses lightweight typed objects instead of introducing dedicated C++
// runtime classes for durations and instants. The "__type" / "__millis"
// convention is the contract that every helper in this file relies on.
std::shared_ptr<LumiereObject> make_typed_object(const std::string &type_name, int64_t millis)
{
    auto object = std::make_shared<LumiereObject>();
    object->fields["__type"] = Value::texte(type_name);
    object->fields["__millis"] = Value::entier(millis);
    return object;
}

bool is_typed_object(const Value &value, const std::string &type_name)
{
    if (!value.is_objet())
    {
        return false;
    }

    const auto object = value.as_objet();
    if (object == nullptr)
    {
        return false;
    }

    const auto type_it = object->fields.find("__type");
    if (type_it == object->fields.end() || !type_it->second.is_texte())
    {
        return false;
    }

    return type_it->second.as_texte() == type_name;
}

int64_t expect_object_millis(IRuntime &runtime,
                             const Value &value,
                             const std::string &type_name,
                             const std::string &context,
                             const RuntimeSite &site)
{
    if (!is_typed_object(value, type_name))
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type " + type_name);
    }

    const auto object = value.as_objet();
    const auto millis_it = object->fields.find("__millis");
    if (millis_it == object->fields.end() || !millis_it->second.is_entier())
    {
        runtime.raise_runtime_error(site, context + " attend une valeur " + type_name + " valide");
    }

    return millis_it->second.as_entier();
}

TimePointMs time_point_from_millis(int64_t millis)
{
    return TimePointMs(std::chrono::milliseconds(millis));
}

int64_t millis_from_time_point(TimePointMs time_point)
{
    return time_point.time_since_epoch().count();
}

struct DateTimeParts
{
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
};

DateTimeParts split_time_point(int64_t millis)
{
    const TimePointMs time_point = time_point_from_millis(millis);
    const auto days = std::chrono::floor<std::chrono::days>(time_point);
    const std::chrono::year_month_day ymd(days);
    const auto time_of_day = std::chrono::hh_mm_ss<std::chrono::milliseconds>(time_point - days);

    DateTimeParts parts;
    parts.year = int(ymd.year());
    parts.month = unsigned(ymd.month());
    parts.day = unsigned(ymd.day());
    parts.hour = int(time_of_day.hours().count());
    parts.minute = int(time_of_day.minutes().count());
    parts.second = int(time_of_day.seconds().count());
    parts.millisecond = int(time_of_day.subseconds().count());
    return parts;
}

std::string zero_pad(int value, int width)
{
    std::ostringstream out;
    out.fill('0');
    out.width(width);
    out << value;
    return out.str();
}

std::string format_instant_string(int64_t millis, const std::string &format)
{
    const DateTimeParts parts = split_time_point(millis);

    std::string result;
    for (std::size_t i = 0; i < format.size();)
    {
        if (format.compare(i, 4, "AAAA") == 0)
        {
            result += zero_pad(parts.year, 4);
            i += 4;
        }
        else if (format.compare(i, 2, "MM") == 0)
        {
            result += zero_pad(int(parts.month), 2);
            i += 2;
        }
        else if (format.compare(i, 2, "JJ") == 0)
        {
            result += zero_pad(int(parts.day), 2);
            i += 2;
        }
        else if (format.compare(i, 2, "HH") == 0)
        {
            result += zero_pad(parts.hour, 2);
            i += 2;
        }
        else if (format.compare(i, 2, "mm") == 0)
        {
            result += zero_pad(parts.minute, 2);
            i += 2;
        }
        else if (format.compare(i, 2, "ss") == 0)
        {
            result += zero_pad(parts.second, 2);
            i += 2;
        }
        else if (format.compare(i, 3, "SSS") == 0)
        {
            result += zero_pad(parts.millisecond, 3);
            i += 3;
        }
        else
        {
            result += format[i++];
        }
    }

    return result;
}

int parse_fixed_int(const std::string &text, std::size_t offset, std::size_t width, const std::string &label)
{
    if (offset + width > text.size())
    {
        throw std::runtime_error("format incomplet pour " + label);
    }

    int value = 0;
    for (std::size_t i = 0; i < width; ++i)
    {
        const char ch = text[offset + i];
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            throw std::runtime_error("caractere inattendu pour " + label);
        }
        value = value * 10 + (ch - '0');
    }
    return value;
}

int64_t parse_instant_string(const std::string &text, const std::string &format)
{
    DateTimeParts parts;
    bool saw_year = false;
    bool saw_month = false;
    bool saw_day = false;

    std::size_t text_index = 0;
    for (std::size_t format_index = 0; format_index < format.size();)
    {
        if (format.compare(format_index, 4, "AAAA") == 0)
        {
            parts.year = parse_fixed_int(text, text_index, 4, "annee");
            format_index += 4;
            text_index += 4;
            saw_year = true;
        }
        else if (format.compare(format_index, 2, "MM") == 0)
        {
            parts.month = static_cast<unsigned>(parse_fixed_int(text, text_index, 2, "mois"));
            format_index += 2;
            text_index += 2;
            saw_month = true;
        }
        else if (format.compare(format_index, 2, "JJ") == 0)
        {
            parts.day = static_cast<unsigned>(parse_fixed_int(text, text_index, 2, "jour"));
            format_index += 2;
            text_index += 2;
            saw_day = true;
        }
        else if (format.compare(format_index, 2, "HH") == 0)
        {
            parts.hour = parse_fixed_int(text, text_index, 2, "heure");
            format_index += 2;
            text_index += 2;
        }
        else if (format.compare(format_index, 2, "mm") == 0)
        {
            parts.minute = parse_fixed_int(text, text_index, 2, "minute");
            format_index += 2;
            text_index += 2;
        }
        else if (format.compare(format_index, 2, "ss") == 0)
        {
            parts.second = parse_fixed_int(text, text_index, 2, "seconde");
            format_index += 2;
            text_index += 2;
        }
        else if (format.compare(format_index, 3, "SSS") == 0)
        {
            parts.millisecond = parse_fixed_int(text, text_index, 3, "milliseconde");
            format_index += 3;
            text_index += 3;
        }
        else
        {
            if (text_index >= text.size() || text[text_index] != format[format_index])
            {
                throw std::runtime_error("separateur inattendu");
            }
            ++format_index;
            ++text_index;
        }
    }

    if (text_index != text.size())
    {
        throw std::runtime_error("texte restant inattendu");
    }

    if (!saw_year || !saw_month || !saw_day)
    {
        throw std::runtime_error("format incomplet");
    }

    const std::chrono::year_month_day ymd(
        std::chrono::year(parts.year),
        std::chrono::month(parts.month),
        std::chrono::day(parts.day));

    if (!ymd.ok() ||
        parts.hour < 0 || parts.hour > 23 ||
        parts.minute < 0 || parts.minute > 59 ||
        parts.second < 0 || parts.second > 59 ||
        parts.millisecond < 0 || parts.millisecond > 999)
    {
        throw std::runtime_error("valeurs de date/heure invalides");
    }

    const auto days = std::chrono::sys_days(ymd);
    const auto duration = std::chrono::hours(parts.hour) +
                          std::chrono::minutes(parts.minute) +
                          std::chrono::seconds(parts.second) +
                          std::chrono::milliseconds(parts.millisecond);
    return millis_from_time_point(TimePointMs(days + duration));
}

Value make_duration_value(int64_t millis, const NativeFunctionFactory &make_native_function);

Value make_instant_value(int64_t millis, const NativeFunctionFactory &make_native_function)
{
    auto object = make_typed_object("Instant", millis);

    object->fields["année"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.année", native_args.site);
            return Value::entier(split_time_point(millis).year);
        }));

    object->fields["mois"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.mois", native_args.site);
            return Value::entier(split_time_point(millis).month);
        }));

    object->fields["jour"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.jour", native_args.site);
            return Value::entier(split_time_point(millis).day);
        }));

    object->fields["heure"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.heure", native_args.site);
            return Value::entier(split_time_point(millis).hour);
        }));

    object->fields["minute"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.minute", native_args.site);
            return Value::entier(split_time_point(millis).minute);
        }));

    object->fields["seconde"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.seconde", native_args.site);
            return Value::entier(split_time_point(millis).second);
        }));

    object->fields["milliseconde"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.milliseconde", native_args.site);
            return Value::entier(split_time_point(millis).millisecond);
        }));

    object->fields["formater"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Instant.formater", native_args.site);
            const std::string format = stdlib_expect_text(runtime, args[0].value, "Instant.formater", native_args.site);
            return Value::texte(format_instant_string(millis, format));
        }));

    object->fields["en_horodatage"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Instant.en_horodatage", native_args.site);
            return Value::entier(millis);
        }));

    object->fields["ajouter"] = Value::fonction(make_native_function(
        [millis, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Instant.ajouter", native_args.site);
            const int64_t duration_ms = expect_object_millis(runtime, args[0].value, "Durée", "Instant.ajouter", native_args.site);
            return make_instant_value(millis + duration_ms, make_native_function);
        }));

    object->fields["soustraire"] = Value::fonction(make_native_function(
        [millis, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Instant.soustraire", native_args.site);
            const int64_t duration_ms = expect_object_millis(runtime, args[0].value, "Durée", "Instant.soustraire", native_args.site);
            return make_instant_value(millis - duration_ms, make_native_function);
        }));

    return Value::objet(std::move(object));
}

Value make_duration_value(int64_t millis, const NativeFunctionFactory &make_native_function)
{
    auto object = make_typed_object("Durée", millis);

    object->fields["en_millisecondes"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Durée.en_millisecondes", native_args.site);
            return Value::entier(millis);
        }));

    object->fields["en_secondes"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Durée.en_secondes", native_args.site);
            return Value::decimal(static_cast<double>(millis) / 1000.0);
        }));

    object->fields["en_minutes"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Durée.en_minutes", native_args.site);
            return Value::decimal(static_cast<double>(millis) / 60000.0);
        }));

    object->fields["en_heures"] = Value::fonction(make_native_function(
        [millis](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Durée.en_heures", native_args.site);
            return Value::decimal(static_cast<double>(millis) / 3600000.0);
        }));

    return Value::objet(std::move(object));
}

int64_t expect_integer_argument(IRuntime &runtime, const NativeArgs &native_args, const std::string &signature)
{
    const auto &args = *native_args.arguments;
    stdlib_expect_positional(runtime, args, 1, signature, native_args.site);
    return stdlib_expect_integer(runtime, args[0].value, signature, native_args.site);
}

}

void register_temps_module(Module &module, const NativeFunctionFactory &make_native_function)
{
    stdlib_bind_public_function(
        module,
        make_native_function,
        "horodatage",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Temps.horodatage", native_args.site);
            const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            return Value::entier(millis_from_time_point(now));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "maintenant",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Temps.maintenant", native_args.site);
            const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            Value instant = make_instant_value(millis_from_time_point(now), make_native_function);
            runtime.annotate_value(instant, "Instant", native_args.site);
            return instant;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "depuis_horodatage",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const int64_t millis = expect_integer_argument(runtime, native_args, "Temps.depuis_horodatage");
            Value instant = make_instant_value(millis, make_native_function);
            runtime.annotate_value(instant, "Instant", native_args.site);
            return instant;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "analyser",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Temps.analyser", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "Temps.analyser", native_args.site);
            const std::string format = stdlib_expect_text(runtime, args[1].value, "Temps.analyser", native_args.site);
            try
            {
                Value instant = make_instant_value(parse_instant_string(text, format), make_native_function);
                runtime.annotate_value(instant, "Instant", native_args.site);
                return instant;
            }
            catch (const std::exception &error)
            {
                runtime.raise_runtime_error(native_args.site, "Temps.analyser a echoue: " + std::string(error.what()));
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "entre",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Temps.entre", native_args.site);
            const int64_t start_ms = expect_object_millis(runtime, args[0].value, "Instant", "Temps.entre", native_args.site);
            const int64_t end_ms = expect_object_millis(runtime, args[1].value, "Instant", "Temps.entre", native_args.site);
            Value duration = make_duration_value(end_ms - start_ms, make_native_function);
            runtime.annotate_value(duration, "Durée", native_args.site);
            return duration;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "attendre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Temps.attendre", native_args.site);
            const int64_t duration_ms = expect_object_millis(runtime, args[0].value, "Durée", "Temps.attendre", native_args.site);
            if (duration_ms < 0)
            {
                runtime.raise_runtime_error(native_args.site, "Temps.attendre attend une duree positive");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
            return Value::rien();
        });

    const auto bind_duration_constructor = [&](const std::string &name, int64_t factor_ms) {
        stdlib_bind_public_function(
            module,
            make_native_function,
            name,
            [name, factor_ms, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                const int64_t amount = expect_integer_argument(runtime, native_args, "Temps." + name);
                Value duration = make_duration_value(amount * factor_ms, make_native_function);
                runtime.annotate_value(duration, "Durée", native_args.site);
                return duration;
            });
    };

    bind_duration_constructor("millisecondes", 1);
    bind_duration_constructor("secondes", 1000);
    bind_duration_constructor("minutes", 60 * 1000);
    bind_duration_constructor("heures", 60 * 60 * 1000);
    bind_duration_constructor("jours", 24 * 60 * 60 * 1000);
}

} // namespace lumiere
