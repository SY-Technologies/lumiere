#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace lumiere
{

namespace
{

using TimePointMs = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

Ref<LumiereObject> make_typed_object(const std::string &type_name, int64_t millis)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = type_name;
    klass->type_identity = native_nominal_type_identity("Temps", type_name);
    object->klass = std::move(klass);
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

    return object->klass != nullptr && object->klass->name == type_name;
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
            throw std::runtime_error("caractère inattendu pour " + label);
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
                throw std::runtime_error("séparateur inattendu");
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
            if (duration_ms > 0 && millis > std::numeric_limits<int64_t>::max() - duration_ms)
            {
                runtime.raise_runtime_error(native_args.site, "Instant.ajouter: le résultat dépasse la limite d'un Instant");
            }
            if (duration_ms < 0 && millis < std::numeric_limits<int64_t>::min() - duration_ms)
            {
                runtime.raise_runtime_error(native_args.site, "Instant.ajouter: le résultat dépasse la limite d'un Instant");
            }
            return make_instant_value(millis + duration_ms, make_native_function);
        }));

    object->fields["soustraire"] = Value::fonction(make_native_function(
        [millis, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Instant.soustraire", native_args.site);
            const int64_t duration_ms = expect_object_millis(runtime, args[0].value, "Durée", "Instant.soustraire", native_args.site);
            if (duration_ms < 0 && millis > std::numeric_limits<int64_t>::max() + duration_ms)
            {
                runtime.raise_runtime_error(native_args.site, "Instant.soustraire: le résultat dépasse la limite d'un Instant");
            }
            if (duration_ms > 0 && millis < std::numeric_limits<int64_t>::min() + duration_ms)
            {
                runtime.raise_runtime_error(native_args.site, "Instant.soustraire: le résultat dépasse la limite d'un Instant");
            }
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

// ---------------------------------------------------------------------------
// IANA timezone database (TZif) support
//
// Reads binary tzdata files (RFC 8536) straight from the host's zoneinfo
// tree, the same files macOS and Linux both ship and the same ones every
// other language's timezone library reads. Only the version 2/3 (64-bit
// transition time) data block is used; a version-1-only file is rejected,
// since every real IANA release since 2005 ships a version 2 block and
// falling back to 32-bit transitions would silently mismatch dates outside
// 1901-2038.
//
// Times beyond the last explicit transition are extrapolated using the
// POSIX TZ footer string every version 2/3 file carries (RFC 8536 section
// 3.3), specifically its "Mm.n.d[/time]" rule form -- the only form real
// zic-generated files use. The Julian-day rule forms ("Jn", "n") are not
// implemented; IANA's own compiler never emits them, so this is not a
// practical limitation, just an honest bound on what was implemented.
// ---------------------------------------------------------------------------

struct TzType
{
    int32_t utoff = 0;
    bool is_dst = false;
};

struct TzTransition
{
    int64_t at = 0; // UTC seconds
    std::size_t type_index = 0;
};

// A POSIX "Mm.n.d[/time]" rule: the transition falls on the n-th occurrence
// (n == 5 meaning "last") of weekday `day` (0 == Sunday) in `month`, at
// `time_seconds` local time.
struct MRule
{
    int month = 0;
    int week = 0;
    int day = 0;
    int64_t time_seconds = 2 * 3600;
};

struct PosixFooter
{
    bool present = false;
    bool has_dst = false;
    bool has_rule = false;
    int32_t std_offset = 0; // already in utoff sign convention (east positive)
    int32_t dst_offset = 0;
    MRule dst_start;
    MRule dst_end;
};

struct TzData
{
    std::vector<TzTransition> transitions; // sorted ascending by `at`
    std::vector<TzType> types;
    PosixFooter footer;
};

uint32_t tz_read_be32(const unsigned char *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

int64_t tz_read_be64(const unsigned char *p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value = (value << 8) | p[i];
    }
    return static_cast<int64_t>(value);
}

struct TzHeader
{
    char version = 0;
    uint32_t isutcnt = 0;
    uint32_t isstdcnt = 0;
    uint32_t leapcnt = 0;
    uint32_t timecnt = 0;
    uint32_t typecnt = 0;
    uint32_t charcnt = 0;
};

TzHeader tz_read_header(const std::string &buffer, std::size_t offset)
{
    if (offset + 44 > buffer.size())
    {
        throw std::runtime_error("fichier TZif tronqué (en-tête)");
    }
    const auto *p = reinterpret_cast<const unsigned char *>(buffer.data()) + offset;
    if (std::memcmp(p, "TZif", 4) != 0)
    {
        throw std::runtime_error("signature TZif invalide");
    }
    TzHeader header;
    header.version = static_cast<char>(p[4]);
    header.isutcnt = tz_read_be32(p + 20);
    header.isstdcnt = tz_read_be32(p + 24);
    header.leapcnt = tz_read_be32(p + 28);
    header.timecnt = tz_read_be32(p + 32);
    header.typecnt = tz_read_be32(p + 36);
    header.charcnt = tz_read_be32(p + 40);
    return header;
}

bool tz_parse_mrule(const std::string &s, std::size_t &i, MRule &rule)
{
    if (i >= s.size() || s[i] != 'M')
    {
        return false;
    }
    ++i;
    const auto read_int = [&](int &out) -> bool {
        const std::size_t start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        {
            ++i;
        }
        if (i == start)
        {
            return false;
        }
        out = std::stoi(s.substr(start, i - start));
        return true;
    };
    if (!read_int(rule.month) || i >= s.size() || s[i] != '.')
    {
        return false;
    }
    ++i;
    if (!read_int(rule.week) || i >= s.size() || s[i] != '.')
    {
        return false;
    }
    ++i;
    if (!read_int(rule.day))
    {
        return false;
    }
    rule.time_seconds = 2 * 3600;
    if (i < s.size() && s[i] == '/')
    {
        ++i;
        bool negative = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        {
            negative = s[i] == '-';
            ++i;
        }
        int hours = 0, minutes = 0, seconds = 0;
        if (!read_int(hours))
        {
            return false;
        }
        if (i < s.size() && s[i] == ':')
        {
            ++i;
            read_int(minutes);
            if (i < s.size() && s[i] == ':')
            {
                ++i;
                read_int(seconds);
            }
        }
        rule.time_seconds = (negative ? -1 : 1) * (int64_t(hours) * 3600 + minutes * 60 + seconds);
    }
    return true;
}

// Parses a POSIX offset field and returns it already negated into the utoff
// sign convention (positive == east of Greenwich): POSIX offsets are
// positive west of Greenwich (UTC = local + offset), so utoff == -offset.
bool tz_parse_posix_offset(const std::string &s, std::size_t &i, int32_t &offset_seconds)
{
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
    {
        negative = s[i] == '-';
        ++i;
    }
    const std::size_t start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
    {
        ++i;
    }
    if (i == start)
    {
        return false;
    }
    const int hours = std::stoi(s.substr(start, i - start));
    int minutes = 0, seconds = 0;
    if (i < s.size() && s[i] == ':')
    {
        ++i;
        const std::size_t minute_start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        {
            ++i;
        }
        minutes = std::stoi(s.substr(minute_start, i - minute_start));
        if (i < s.size() && s[i] == ':')
        {
            ++i;
            const std::size_t second_start = i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
            {
                ++i;
            }
            seconds = std::stoi(s.substr(second_start, i - second_start));
        }
    }
    const int32_t total = hours * 3600 + minutes * 60 + seconds;
    offset_seconds = negative ? total : -total;
    return true;
}

std::string tz_parse_posix_name(const std::string &s, std::size_t &i)
{
    std::string name;
    if (i < s.size() && s[i] == '<')
    {
        ++i;
        while (i < s.size() && s[i] != '>')
        {
            name += s[i++];
        }
        if (i < s.size())
        {
            ++i;
        }
        return name;
    }
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])))
    {
        name += s[i++];
    }
    return name;
}

PosixFooter tz_parse_posix_footer(const std::string &s)
{
    PosixFooter footer;
    if (s.empty())
    {
        return footer;
    }
    std::size_t i = 0;
    if (tz_parse_posix_name(s, i).empty())
    {
        return footer;
    }
    if (!tz_parse_posix_offset(s, i, footer.std_offset))
    {
        return footer;
    }
    footer.present = true;
    if (i < s.size() && s[i] != ',')
    {
        if (!tz_parse_posix_name(s, i).empty())
        {
            footer.has_dst = true;
            if (i < s.size() && s[i] != ',')
            {
                tz_parse_posix_offset(s, i, footer.dst_offset);
            }
            else
            {
                footer.dst_offset = footer.std_offset + 3600;
            }
        }
    }
    if (footer.has_dst && i < s.size() && s[i] == ',')
    {
        ++i;
        if (tz_parse_mrule(s, i, footer.dst_start) && i < s.size() && s[i] == ',')
        {
            ++i;
            if (tz_parse_mrule(s, i, footer.dst_end))
            {
                footer.has_rule = true;
            }
        }
    }
    return footer;
}

// The directory the running executable lives in, so the bundled
// third_party/zoneinfo (see its README) can be found next to it regardless
// of the current working directory or install location. This is what lets
// named-timezone lookups work on Windows, which ships no tzdata of its own.
std::filesystem::path executable_directory()
{
    std::error_code ec;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return {};
    }
    const auto resolved = std::filesystem::canonical(buffer, ec);
    return ec ? std::filesystem::path() : resolved.parent_path();
#else
    const auto resolved = std::filesystem::canonical("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : resolved.parent_path();
#endif
}

std::string tz_zoneinfo_root()
{
    if (const char *dir = std::getenv("TZDIR"); dir != nullptr && *dir != '\0')
    {
        std::string root = dir;
        if (root.back() != '/')
        {
            root += '/';
        }
        return root;
    }

    // The bundled copy ships identically on every platform (see
    // third_party/zoneinfo/README.md) so Fuseau() behaves the same
    // regardless of what, if anything, the host OS provides -- computed
    // once, since it never changes within a process's lifetime.
    static const std::string bundled = []() -> std::string {
        const std::filesystem::path candidate = executable_directory() / "zoneinfo";
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec))
        {
            return candidate.generic_string() + "/";
        }
        return std::string();
    }();
    if (!bundled.empty())
    {
        return bundled;
    }

#ifdef _WIN32
    // No bundled copy found and no system tzdata to fall back to.
    return std::string();
#else
    return "/usr/share/zoneinfo/";
#endif
}

// Only IANA zone-name characters are accepted (letters, digits, '_' '+' '-'
// '.' '/'), and no path segment may be empty, "." or "..". This is what
// keeps a Lumiere-supplied Texte from ever escaping the zoneinfo directory.
bool tz_is_safe_zone_name(const std::string &name)
{
    if (name.empty() || name.front() == '/' || name.back() == '/')
    {
        return false;
    }
    std::size_t segment_start = 0;
    for (std::size_t i = 0; i <= name.size(); ++i)
    {
        if (i == name.size() || name[i] == '/')
        {
            const std::string segment = name.substr(segment_start, i - segment_start);
            if (segment.empty() || segment == "." || segment == "..")
            {
                return false;
            }
            segment_start = i + 1;
            continue;
        }
        const char c = name[i];
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '+' || c == '-' || c == '.';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

TzData tz_load_zone(const std::string &name)
{
    if (!tz_is_safe_zone_name(name))
    {
        throw std::runtime_error("nom de fuseau horaire invalide: \"" + name + "\"");
    }

    const std::string path = tz_zoneinfo_root() + name;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("fuseau horaire introuvable: " + name);
    }
    const std::string buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (buffer.size() < 44)
    {
        throw std::runtime_error("fichier TZif trop court pour: " + name);
    }

    const TzHeader header1 = tz_read_header(buffer, 0);
    if (header1.version == 0)
    {
        throw std::runtime_error("format TZif version 1 non pris en charge pour: " + name);
    }
    const std::size_t block1_size = 44 + header1.timecnt * 4 + header1.timecnt * 1 + header1.typecnt * 6 +
                                    header1.charcnt + header1.leapcnt * 8 + header1.isstdcnt + header1.isutcnt;
    const TzHeader header2 = tz_read_header(buffer, block1_size);
    std::size_t p = block1_size + 44;

    const auto *base = reinterpret_cast<const unsigned char *>(buffer.data());
    if (p + header2.timecnt * 8 > buffer.size())
    {
        throw std::runtime_error("fichier TZif tronqué (transitions) pour: " + name);
    }
    std::vector<int64_t> transition_times(header2.timecnt);
    for (uint32_t i = 0; i < header2.timecnt; ++i)
    {
        transition_times[i] = tz_read_be64(base + p + i * 8);
    }
    p += header2.timecnt * 8;

    if (p + header2.timecnt > buffer.size())
    {
        throw std::runtime_error("fichier TZif tronqué (types de transition) pour: " + name);
    }
    std::vector<uint8_t> transition_types(header2.timecnt);
    for (uint32_t i = 0; i < header2.timecnt; ++i)
    {
        transition_types[i] = base[p + i];
    }
    p += header2.timecnt;

    if (p + header2.typecnt * 6 > buffer.size())
    {
        throw std::runtime_error("fichier TZif tronqué (ttinfo) pour: " + name);
    }
    TzData data;
    data.types.resize(header2.typecnt);
    for (uint32_t i = 0; i < header2.typecnt; ++i)
    {
        const int32_t utoff = static_cast<int32_t>(tz_read_be32(base + p));
        const uint8_t is_dst = base[p + 4];
        data.types[i] = TzType{utoff, is_dst != 0};
        p += 6;
    }
    // Skip designation characters, leap seconds, and the std/wall and
    // ut/local indicator arrays: none of them affect a plain offset lookup.
    p += header2.charcnt;
    p += static_cast<std::size_t>(header2.leapcnt) * 12;
    p += header2.isstdcnt;
    p += header2.isutcnt;

    data.transitions.resize(header2.timecnt);
    for (uint32_t i = 0; i < header2.timecnt; ++i)
    {
        data.transitions[i] = TzTransition{transition_times[i], transition_types[i]};
    }

    if (p < buffer.size() && base[p] == '\n')
    {
        const std::size_t closing = buffer.find('\n', p + 1);
        if (closing != std::string::npos)
        {
            data.footer = tz_parse_posix_footer(buffer.substr(p + 1, closing - (p + 1)));
        }
    }

    return data;
}

// The instant, in UTC milliseconds, at which an MRule fires in `year`, given
// the UTC offset (utoff seconds) in effect just before the transition (the
// rule's local time-of-day is wall-clock time under that offset).
int64_t tz_rule_instant_millis(const MRule &rule, int year, int32_t offset_before_seconds)
{
    using namespace std::chrono;
    const sys_days first_of_month = sys_days{year_month_day{
        std::chrono::year(year), std::chrono::month(static_cast<unsigned>(rule.month)), std::chrono::day(1)}};
    sys_days target_day;
    if (rule.week == 5)
    {
        const year_month_day_last last{
            std::chrono::year(year), month_day_last{std::chrono::month(static_cast<unsigned>(rule.month))}};
        const sys_days last_day = sys_days(last);
        const weekday last_weekday{last_day};
        int diff = int(last_weekday.c_encoding()) - rule.day;
        if (diff < 0)
        {
            diff += 7;
        }
        target_day = last_day - std::chrono::days(diff);
    }
    else
    {
        const weekday first_weekday{first_of_month};
        int diff = rule.day - int(first_weekday.c_encoding());
        if (diff < 0)
        {
            diff += 7;
        }
        target_day = first_of_month + std::chrono::days(diff) + std::chrono::days((rule.week - 1) * 7);
    }
    const int64_t local_seconds = target_day.time_since_epoch().count() * 86400 + rule.time_seconds;
    return (local_seconds - offset_before_seconds) * 1000;
}

int tz_civil_year_from_millis(int64_t millis)
{
    const auto days_point = std::chrono::floor<std::chrono::days>(time_point_from_millis(millis));
    const std::chrono::year_month_day ymd(days_point);
    return int(ymd.year());
}

// Offset (utoff seconds, east-of-Greenwich positive) in effect at a UTC
// instant. Times before the first transition (or a zone with none) fall
// back to the first non-DST type, matching RFC 8536's own convention. Times
// past the last explicit transition are extrapolated with the POSIX footer.
int32_t tz_offset_for_instant(const TzData &data, int64_t utc_millis)
{
    const int64_t utc_seconds = utc_millis >= 0 ? utc_millis / 1000 : -((-utc_millis + 999) / 1000);

    const auto first_standard_offset = [&]() -> int32_t {
        for (const TzType &type : data.types)
        {
            if (!type.is_dst)
            {
                return type.utoff;
            }
        }
        return data.types.empty() ? 0 : data.types.front().utoff;
    };

    if (data.transitions.empty())
    {
        return first_standard_offset();
    }
    if (utc_seconds < data.transitions.front().at)
    {
        return first_standard_offset();
    }

    const auto it = std::upper_bound(
        data.transitions.begin(), data.transitions.end(), utc_seconds,
        [](int64_t value, const TzTransition &transition) { return value < transition.at; });
    const auto active = it - 1;
    const bool beyond_last = (active + 1 == data.transitions.end());

    if (beyond_last && data.footer.present)
    {
        if (!data.footer.has_dst)
        {
            return data.footer.std_offset;
        }
        if (data.footer.has_rule)
        {
            const int year = tz_civil_year_from_millis(utc_millis);
            const int64_t start = tz_rule_instant_millis(data.footer.dst_start, year, data.footer.std_offset) / 1000;
            const int64_t end = tz_rule_instant_millis(data.footer.dst_end, year, data.footer.dst_offset) / 1000;
            const bool dst_active = (start <= end) ? (utc_seconds >= start && utc_seconds < end)
                                                    : (utc_seconds >= start || utc_seconds < end);
            return dst_active ? data.footer.dst_offset : data.footer.std_offset;
        }
        return data.footer.std_offset;
    }

    return data.types[active->type_index].utoff;
}

// ---------------------------------------------------------------------------
// Fuseau / DateHeure / RepèreMonotone runtime values
// ---------------------------------------------------------------------------

struct TzZoneState : NativeState
{
    TzData data;

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

Value make_fuseau_value(const std::string &name, TzData data, const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "Temps.Fuseau";
    klass->type_identity = native_nominal_type_identity("Temps", "Fuseau");
    object->klass = std::move(klass);
    auto state = make_ref<TzZoneState>();
    state->data = std::move(data);
    object->native_state = std::move(state);

    object->fields["nom"] = Value::fonction(make_native_function(
        [name](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Fuseau.nom", native_args.site);
            return Value::texte(name);
        }));

    return Value::objet(std::move(object));
}

bool is_fuseau_object(const Value &value)
{
    return value.is_objet() && value.as_objet() != nullptr && value.as_objet()->klass != nullptr &&
           value.as_objet()->klass->name == "Temps.Fuseau";
}

const TzData &expect_fuseau(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!is_fuseau_object(value))
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type Fuseau");
    }
    auto *state = dynamic_cast<TzZoneState *>(value.as_objet()->native_state.get());
    if (state == nullptr)
    {
        runtime.raise_runtime_error(site, context + " attend une valeur Fuseau valide");
    }
    return state->data;
}

Value make_date_heure_value(int64_t instant_millis,
                            int32_t offset_seconds,
                            const Value &fuseau_value,
                            const NativeFunctionFactory &make_native_function)
{
    const int64_t local_millis = instant_millis + int64_t(offset_seconds) * 1000;
    const DateTimeParts parts = split_time_point(local_millis);

    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "Temps.DateHeure";
    klass->type_identity = native_nominal_type_identity("Temps", "DateHeure");
    object->klass = std::move(klass);

    const auto bind_field = [&](const std::string &field_name, int64_t field_value) {
        object->fields[field_name] = Value::fonction(make_native_function(
            [field_name, field_value](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                stdlib_expect_positional(runtime, *native_args.arguments, 0, "DateHeure." + field_name, native_args.site);
                return Value::entier(field_value);
            }));
    };
    bind_field("année", parts.year);
    bind_field("mois", parts.month);
    bind_field("jour", parts.day);
    bind_field("heure", parts.hour);
    bind_field("minute", parts.minute);
    bind_field("seconde", parts.second);
    bind_field("milliseconde", parts.millisecond);
    bind_field("décalage_utc_secondes", offset_seconds);

    object->fields["instant"] = Value::fonction(make_native_function(
        [instant_millis, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "DateHeure.instant", native_args.site);
            return make_instant_value(instant_millis, make_native_function);
        }));

    // The closure below returns the very Fuseau it was built with. A native
    // handler is an opaque std::function, so the object it captures has to be
    // reached through LumiereFunction::native_captures as well, or the cycle
    // collector could never trace into it (see value.hpp's own comment on
    // native_captures for why this split -- raw pointer in the closure, owning
    // Ref alongside it -- is the required shape).
    const Ref<LumiereObject> fuseau_object = fuseau_value.as_objet();
    LumiereObject *const fuseau_raw = fuseau_object.get();
    Value fuseau_field = Value::fonction(make_native_function(
        [fuseau_raw](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "DateHeure.fuseau", native_args.site);
            return Value::objet(Ref<LumiereObject>(fuseau_raw));
        }));
    fuseau_field.as_fonction()->native_captures.push_back(fuseau_object);
    object->fields["fuseau"] = std::move(fuseau_field);

    return Value::objet(std::move(object));
}

Value make_repere_value(int64_t steady_nanos)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "Temps.RepèreMonotone";
    klass->type_identity = native_nominal_type_identity("Temps", "RepèreMonotone");
    object->klass = std::move(klass);
    // Deliberately opaque: no accessor is bound, matching the documented
    // contract that a RepèreMonotone cannot be serialized, formatted, or
    // compared with an Instant, and is meaningful only inside this process.
    object->fields["__repère_nanos"] = Value::entier(steady_nanos);
    return Value::objet(std::move(object));
}

int64_t expect_repere_nanos(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!value.is_objet() || value.as_objet() == nullptr || value.as_objet()->klass == nullptr ||
        value.as_objet()->klass->name != "Temps.RepèreMonotone")
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type RepèreMonotone");
    }
    const auto object = value.as_objet();
    const auto it = object->fields.find("__repère_nanos");
    if (it == object->fields.end() || !it->second.is_entier())
    {
        runtime.raise_runtime_error(site, context + " attend une valeur RepèreMonotone valide");
    }
    return it->second.as_entier();
}

int64_t steady_now_nanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------
// ISO 8601 parsing and formatting
// ---------------------------------------------------------------------------

// "YYYY-MM-DDTHH:MM:SS[.fraction](Z|±HH:MM|±HHMM|±HH)". A missing zone is
// rejected rather than assumed local, per docs/stdlib-foundations.md: a
// timestamp with no zone is ambiguous, not defaultable.
int64_t parse_iso8601_string(const std::string &text)
{
    std::size_t i = 0;
    const auto need = [&](std::size_t n) {
        if (i + n > text.size())
        {
            throw std::runtime_error("format ISO 8601 incomplet");
        }
    };
    const auto expect_char = [&](char expected, const char *label) {
        need(1);
        if (text[i] != expected)
        {
            throw std::runtime_error(std::string("séparateur '") + label + "' attendu");
        }
        ++i;
    };

    need(10);
    const int year = parse_fixed_int(text, i, 4, "année");
    i += 4;
    expect_char('-', "-");
    const unsigned month = static_cast<unsigned>(parse_fixed_int(text, i, 2, "mois"));
    i += 2;
    expect_char('-', "-");
    const unsigned day = static_cast<unsigned>(parse_fixed_int(text, i, 2, "jour"));
    i += 2;
    expect_char('T', "T");
    need(8);
    const int hour = parse_fixed_int(text, i, 2, "heure");
    i += 2;
    expect_char(':', ":");
    const int minute = parse_fixed_int(text, i, 2, "minute");
    i += 2;
    expect_char(':', ":");
    const int second = parse_fixed_int(text, i, 2, "seconde");
    i += 2;

    int millisecond = 0;
    if (i < text.size() && text[i] == '.')
    {
        ++i;
        const std::size_t fraction_start = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        if (i == fraction_start)
        {
            throw std::runtime_error("fraction de seconde invalide");
        }
        std::string fraction = text.substr(fraction_start, std::min<std::size_t>(i - fraction_start, 3));
        while (fraction.size() < 3)
        {
            fraction += '0';
        }
        millisecond = std::stoi(fraction);
    }

    if (i >= text.size())
    {
        throw std::runtime_error("fuseau UTC requis (Z ou décalage numérique)");
    }
    int offset_seconds = 0;
    if (text[i] == 'Z' || text[i] == 'z')
    {
        ++i;
    }
    else if (text[i] == '+' || text[i] == '-')
    {
        const bool negative = text[i] == '-';
        ++i;
        need(2);
        const int offset_hour = parse_fixed_int(text, i, 2, "décalage (heures)");
        i += 2;
        int offset_minute = 0;
        if (i < text.size() && text[i] == ':')
        {
            ++i;
            offset_minute = parse_fixed_int(text, i, 2, "décalage (minutes)");
            i += 2;
        }
        else if (i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) &&
                std::isdigit(static_cast<unsigned char>(text[i + 1])))
        {
            offset_minute = parse_fixed_int(text, i, 2, "décalage (minutes)");
            i += 2;
        }
        offset_seconds = (negative ? -1 : 1) * (offset_hour * 3600 + offset_minute * 60);
    }
    else
    {
        throw std::runtime_error("fuseau UTC requis (Z ou décalage numérique)");
    }

    if (i != text.size())
    {
        throw std::runtime_error("texte restant inattendu après le fuseau");
    }

    const std::chrono::year_month_day ymd{std::chrono::year(year), std::chrono::month(month), std::chrono::day(day)};
    if (!ymd.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
    {
        throw std::runtime_error("valeurs de date/heure invalides");
    }

    const auto days = std::chrono::sys_days(ymd);
    const auto duration = std::chrono::hours(hour) + std::chrono::minutes(minute) +
                          std::chrono::seconds(second) + std::chrono::milliseconds(millisecond);
    const int64_t local_millis = millis_from_time_point(TimePointMs(days + duration));
    return local_millis - int64_t(offset_seconds) * 1000;
}

std::string format_iso8601_string(int64_t millis)
{
    const DateTimeParts parts = split_time_point(millis);
    std::ostringstream out;
    out << zero_pad(parts.year, 4) << '-' << zero_pad(int(parts.month), 2) << '-' << zero_pad(int(parts.day), 2)
        << 'T' << zero_pad(parts.hour, 2) << ':' << zero_pad(parts.minute, 2) << ':' << zero_pad(parts.second, 2)
        << '.' << zero_pad(parts.millisecond, 3) << 'Z';
    return out.str();
}

// Best-effort host-timezone discovery: the TZ environment variable, then the
// /etc/localtime symlink's target (the convention both Linux and macOS use),
// then Debian-style /etc/timezone. Failing to identify one is reported
// explicitly rather than silently defaulting to UTC.
std::optional<std::string> discover_local_zone_name()
{
    if (const char *tz = std::getenv("TZ"); tz != nullptr && *tz != '\0')
    {
        std::string name = tz;
        if (!name.empty() && name.front() == ':')
        {
            name.erase(0, 1);
        }
        if (!name.empty() && tz_is_safe_zone_name(name))
        {
            std::error_code ec;
            if (std::filesystem::exists(tz_zoneinfo_root() + name, ec))
            {
                return name;
            }
        }
    }

    std::error_code ec;
    const std::filesystem::path link_target = std::filesystem::read_symlink("/etc/localtime", ec);
    if (!ec)
    {
        const std::string target = link_target.string();
        const std::string marker = "zoneinfo/";
        const std::size_t position = target.rfind(marker);
        if (position != std::string::npos)
        {
            return target.substr(position + marker.size());
        }
    }

    std::ifstream timezone_file("/etc/timezone");
    if (timezone_file)
    {
        std::string line;
        if (std::getline(timezone_file, line))
        {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                return line;
            }
        }
    }

    return std::nullopt;
}


}

void register_temps_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
    stdlib_bind_public_type(module, "Instant");
    stdlib_bind_public_type(module, "Durée");
    auto error_class = make_ref<LumiereClass>();
    error_class->name = "Temps.ErreurTemps";
    stdlib_bind_public_value(
        module,
        "ErreurTemps",
        Value::classe(std::move(error_class)));
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
            runtime.annotate_value(instant, "Temps.Instant", native_args.site);
            return instant;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "depuis_horodatage",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const int64_t millis = expect_integer_argument(runtime, native_args, "Temps.depuis_horodatage");
            Value instant = make_instant_value(millis, make_native_function);
            runtime.annotate_value(instant, "Temps.Instant", native_args.site);
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
                runtime.annotate_value(instant, "Temps.Instant", native_args.site);
                return stdlib_success(std::move(instant));
            }
            catch (const std::exception &error)
            {
                return stdlib_failure(
                    stdlib_error_value(
                        "Temps.ErreurTemps",
                        "analyser",
                        error.what()),
                    native_args.site);
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
            if (start_ms > 0)
            {
                if (end_ms < std::numeric_limits<int64_t>::min() + start_ms)
                {
                    runtime.raise_runtime_error(native_args.site, "Temps.entre: le calcul de duree dépasse les limites");
                }
            }
            else if (start_ms < 0)
            {
                if (end_ms > std::numeric_limits<int64_t>::max() + start_ms)
                {
                    runtime.raise_runtime_error(native_args.site, "Temps.entre: le calcul de duree dépasse les limites");
                }
            }
            Value duration = make_duration_value(end_ms - start_ms, make_native_function);
            runtime.annotate_value(duration, "Temps.Durée", native_args.site);
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
                runtime.raise_runtime_error(native_args.site, "Temps.attendre attend une durée positive");
            }
            constexpr int64_t kMaxSleepMs = 24LL * 60 * 60 * 1000;
            if (duration_ms > kMaxSleepMs)
            {
                runtime.raise_runtime_error(native_args.site, "Temps.attendre: la duree dépasse le maximum autorise (24h)");
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
                if (amount > 0 && factor_ms > 0 && amount > std::numeric_limits<int64_t>::max() / factor_ms)
                {
                    runtime.raise_runtime_error(native_args.site, "Temps." + name + ": le résultat dépasse la limite d'une Duree");
                }
                if (amount < 0 && factor_ms > 0 && amount < std::numeric_limits<int64_t>::min() / factor_ms)
                {
                    runtime.raise_runtime_error(native_args.site, "Temps." + name + ": le résultat dépasse la limite d'une Duree");
                }
                Value duration = make_duration_value(amount * factor_ms, make_native_function);
                runtime.annotate_value(duration, "Temps.Durée", native_args.site);
                return duration;
            });
    };

    bind_duration_constructor("millisecondes", 1);
    bind_duration_constructor("secondes", 1000);
    bind_duration_constructor("minutes", 60 * 1000);
    bind_duration_constructor("heures", 60 * 60 * 1000);
    bind_duration_constructor("jours", 24 * 60 * 60 * 1000);

    stdlib_bind_public_type(module, "Fuseau");
    stdlib_bind_public_type(module, "DateHeure");
    stdlib_bind_public_type(module, "RepèreMonotone");

    stdlib_bind_public_function(
        module,
        make_native_function,
        "analyser_iso8601",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Temps.analyser_iso8601", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "Temps.analyser_iso8601", native_args.site);
            try
            {
                Value instant = make_instant_value(parse_iso8601_string(text), make_native_function);
                runtime.annotate_value(instant, "Temps.Instant", native_args.site);
                return stdlib_success(std::move(instant));
            }
            catch (const std::exception &error)
            {
                return stdlib_failure(
                    stdlib_error_value("Temps.ErreurTemps", "analyser_iso8601", error.what()),
                    native_args.site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "formater_iso8601",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Temps.formater_iso8601", native_args.site);
            const int64_t millis =
                expect_object_millis(runtime, args[0].value, "Instant", "Temps.formater_iso8601", native_args.site);
            return Value::texte(format_iso8601_string(millis));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "fuseau",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Temps.fuseau", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Temps.fuseau", native_args.site);
            try
            {
                TzData data = tz_load_zone(name);
                Value zone = make_fuseau_value(name, std::move(data), make_native_function);
                runtime.annotate_value(zone, "Temps.Fuseau", native_args.site);
                return stdlib_success(std::move(zone));
            }
            catch (const std::exception &error)
            {
                return stdlib_failure(
                    stdlib_error_value("Temps.ErreurTemps", "fuseau", error.what()),
                    native_args.site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "fuseau_local",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Temps.fuseau_local", native_args.site);
            try
            {
                const std::optional<std::string> name = discover_local_zone_name();
                if (!name.has_value())
                {
                    return stdlib_failure(
                        stdlib_error_value(
                            "Temps.ErreurTemps",
                            "fuseau_local",
                            "impossible de déterminer le fuseau horaire local de cette machine"),
                        native_args.site);
                }
                TzData data = tz_load_zone(*name);
                Value zone = make_fuseau_value(*name, std::move(data), make_native_function);
                runtime.annotate_value(zone, "Temps.Fuseau", native_args.site);
                return stdlib_success(std::move(zone));
            }
            catch (const std::exception &error)
            {
                return stdlib_failure(
                    stdlib_error_value("Temps.ErreurTemps", "fuseau_local", error.what()),
                    native_args.site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "dans_fuseau",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Temps.dans_fuseau", native_args.site);
            const int64_t instant_millis =
                expect_object_millis(runtime, args[0].value, "Instant", "Temps.dans_fuseau", native_args.site);
            const TzData &zone_data = expect_fuseau(runtime, args[1].value, "Temps.dans_fuseau", native_args.site);
            const int32_t offset_seconds = tz_offset_for_instant(zone_data, instant_millis);
            Value date_heure =
                make_date_heure_value(instant_millis, offset_seconds, args[1].value, make_native_function);
            runtime.annotate_value(date_heure, "Temps.DateHeure", native_args.site);
            return date_heure;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "repère",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Temps.repère", native_args.site);
            Value repere = make_repere_value(steady_now_nanos());
            runtime.annotate_value(repere, "Temps.RepèreMonotone", native_args.site);
            return repere;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "écoulé",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Temps.écoulé", native_args.site);
            const int64_t since_nanos =
                expect_repere_nanos(runtime, args[0].value, "Temps.écoulé", native_args.site);
            const int64_t elapsed_nanos = steady_now_nanos() - since_nanos;
            const int64_t elapsed_millis = elapsed_nanos > 0 ? elapsed_nanos / 1'000'000 : 0;
            Value duration = make_duration_value(elapsed_millis, make_native_function);
            runtime.annotate_value(duration, "Temps.Durée", native_args.site);
            return duration;
        });
}

} // namespace lumiere
