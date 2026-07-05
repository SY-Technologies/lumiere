#include "../luminet_shared.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace lumiere
{

namespace
{

bool contains_http_separator_or_control(const std::string &text)
{
    for (unsigned char ch : text)
    {
        if (ch <= 31 || ch == 127)
        {
            return true;
        }
        switch (ch)
        {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
        case ' ':
        case '\t':
            return true;
        default:
            break;
        }
    }
    return false;
}

void validate_http_header(IRuntime &runtime,
                          const std::string &name,
                          const std::string &value,
                          const std::string &context,
                          const RuntimeSite &site)
{
    if (name.empty() || contains_http_separator_or_control(name))
    {
        runtime.raise_runtime_error(site, context + " requiert un nom d'en-tête HTTP valide");
    }
    if (value.find('\r') != std::string::npos || value.find('\n') != std::string::npos)
    {
        runtime.raise_runtime_error(site, context + " requiert une valeur d'en-tête HTTP sur une seule ligne");
    }
}

std::string http_reason_phrase(int64_t status)
{
    switch (status)
    {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "HTTP Response";
    }
}

} // namespace

Value make_http_response_value(IRuntime &runtime,
                               int64_t status,
                               const std::string &body,
                               const std::vector<std::pair<std::string, std::string>> &headers,
                               const NativeFunctionFactory &make_native_function,
                               const RuntimeSite &site)
{
    auto object = make_hidden_typed_object("RéponseHTTP");
    object->fields["statut"] = Value::entier(status);
    object->fields["corps"] = Value::texte(body);
    object->fields["succès"] = Value::logique(status >= 200 && status <= 299);
    object->fields["entête"] = Value::fonction(make_native_function(
        [headers](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "RéponseHTTP.entête", native_args.site);
            return Value::texte(header_value_or_empty(headers,
                                                      stdlib_expect_text(inner_runtime, args[0].value, "RéponseHTTP.entête", native_args.site)));
        }));
    object->fields["entêtes"] = Value::fonction(make_native_function(
        [headers](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(inner_runtime, *native_args.arguments, 0, "RéponseHTTP.entêtes", native_args.site);
            return make_text_dictionary_value(inner_runtime, headers, native_args.site);
        }));
    Value result = Value::objet(std::move(object));
    runtime.annotate_value(result, "RéponseHTTP", site);
    return result;
}

Value make_http_request_value(IRuntime &runtime,
                              const HttpRequestData &request,
                              const NativeFunctionFactory &make_native_function,
                              const RuntimeSite &site)
{
    auto object = make_hidden_typed_object("RequêteHTTP");
    object->fields["méthode"] = Value::texte(request.french_method);
    object->fields["chemin"] = Value::texte(request.path);
    object->fields["corps"] = Value::texte(request.body);
    object->fields["paramètre"] = Value::fonction(make_native_function(
        [request](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "RequêteHTTP.paramètre", native_args.site);
            const std::string name = stdlib_expect_text(inner_runtime, args[0].value, "RequêteHTTP.paramètre", native_args.site);
            for (const auto &entry : request.route_params)
            {
                if (entry.first == name)
                {
                    return Value::texte(entry.second);
                }
            }
            return Value::texte("");
        }));
    object->fields["requête"] = Value::fonction(make_native_function(
        [request](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(inner_runtime, args, 1, 2, "RequêteHTTP.requête", native_args.site);
            const std::string name = stdlib_expect_text(inner_runtime, args[0].value, "RequêteHTTP.requête", native_args.site);
            const std::string fallback = args.size() == 2
                ? stdlib_expect_text(inner_runtime, args[1].value, "RequêteHTTP.requête", native_args.site)
                : "";
            for (const auto &entry : request.query_params)
            {
                if (entry.first == name)
                {
                    return Value::texte(entry.second);
                }
            }
            return Value::texte(fallback);
        }));
    object->fields["entête"] = Value::fonction(make_native_function(
        [request](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "RequêteHTTP.entête", native_args.site);
            return Value::texte(header_value_or_empty(request.headers,
                                                      stdlib_expect_text(inner_runtime, args[0].value, "RequêteHTTP.entête", native_args.site)));
        }));
    object->fields["entêtes"] = Value::fonction(make_native_function(
        [request](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(inner_runtime, *native_args.arguments, 0, "RequêteHTTP.entêtes", native_args.site);
            return make_text_dictionary_value(inner_runtime, request.headers, native_args.site);
        }));
    Value result = Value::objet(std::move(object));
    runtime.annotate_value(result, "RequêteHTTP", site);
    return result;
}

std::string guess_content_type(const std::string &path)
{
    const std::string lower = to_lower_ascii(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".htm")
    {
        return "text/html; charset=utf-8";
    }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".html")
    {
        return "text/html; charset=utf-8";
    }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json")
    {
        return "application/json";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt")
    {
        return "text/plain; charset=utf-8";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".css")
    {
        return "text/css; charset=utf-8";
    }
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".js")
    {
        return "text/javascript; charset=utf-8";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mjs")
    {
        return "text/javascript; charset=utf-8";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".svg")
    {
        return "image/svg+xml";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png")
    {
        return "image/png";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg")
    {
        return "image/jpeg";
    }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg")
    {
        return "image/jpeg";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".gif")
    {
        return "image/gif";
    }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".webp")
    {
        return "image/webp";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ico")
    {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

void send_http_response(IRuntime &runtime,
                        const std::shared_ptr<HttpResponseWriterState> &state,
                        int64_t status,
                        const std::string &body,
                        std::vector<std::pair<std::string, std::string>> headers,
                        const std::string &context,
                        const RuntimeSite &site)
{
    if (state->sent)
    {
        runtime.raise_runtime_error(site, context + " ne peut envoyer qu'une seule réponse");
    }

    bool has_type = false;
    bool has_length = false;
    for (const auto &header : state->headers)
    {
        validate_http_header(runtime, header.first, header.second, context, site);
        const std::string lower = to_lower_ascii(header.first);
        if (lower == "content-length" || lower == "connection")
        {
            runtime.raise_runtime_error(site, context + " ne permet pas de surcharger les en-têtes Content-Length ou Connection");
        }
        if (lower == "content-type")
        {
            has_type = true;
        }
    }
    for (const auto &header : headers)
    {
        validate_http_header(runtime, header.first, header.second, context, site);
        const std::string lower = to_lower_ascii(header.first);
        if (lower == "content-type")
        {
            has_type = true;
        }
        if (lower == "content-length")
        {
            has_length = true;
        }
    }
    if (!has_type)
    {
        headers.push_back({"Content-Type", "text/plain; charset=utf-8"});
    }
    if (!has_length)
    {
        headers.push_back({"Content-Length", std::to_string(body.size())});
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << http_reason_phrase(status) << "\r\n";
    for (const auto &header : state->headers)
    {
        headers.push_back(header);
    }
    headers.push_back({"Connection", "close"});
    for (const auto &header : headers)
    {
        response << header.first << ": " << header.second << "\r\n";
    }
    response << "\r\n";
    response << body;

    const std::string payload = response.str();
    send_all(runtime,
             state->fd,
             reinterpret_cast<const unsigned char *>(payload.data()),
             payload.size(),
             context,
             site);
    state->sent = true;
}

Value make_http_response_writer_value(IRuntime &runtime,
                                      const std::shared_ptr<HttpResponseWriterState> &state,
                                      const NativeFunctionFactory &make_native_function,
                                      const RuntimeSite &site)
{
    auto object = make_hidden_typed_object("RéponseServeurHTTP");
    attach_native_state(object, state);

    object->fields["définir_entête"] = Value::fonction(make_native_function(
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 2, "RéponseServeurHTTP.définir_entête", native_args.site);
            const std::string name = stdlib_expect_text(inner_runtime, args[0].value, "RéponseServeurHTTP.définir_entête", native_args.site);
            const std::string value = stdlib_expect_text(inner_runtime, args[1].value, "RéponseServeurHTTP.définir_entête", native_args.site);
            validate_http_header(inner_runtime, name, value, "RéponseServeurHTTP.définir_entête", native_args.site);
            state->headers.push_back({name, value});
            return Value::rien();
        }));

    object->fields["envoyer"] = Value::fonction(make_native_function(
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 2, "RéponseServeurHTTP.envoyer", native_args.site);
            send_http_response(inner_runtime,
                               state,
                               stdlib_expect_integer(inner_runtime, args[0].value, "RéponseServeurHTTP.envoyer", native_args.site),
                               stdlib_expect_text(inner_runtime, args[1].value, "RéponseServeurHTTP.envoyer", native_args.site),
                               {},
                               "RéponseServeurHTTP.envoyer",
                               native_args.site);
            return Value::rien();
        }));

    object->fields["envoyer_json"] = Value::fonction(make_native_function(
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 2, "RéponseServeurHTTP.envoyer_json", native_args.site);
            send_http_response(inner_runtime,
                               state,
                               stdlib_expect_integer(inner_runtime, args[0].value, "RéponseServeurHTTP.envoyer_json", native_args.site),
                               stdlib_expect_text(inner_runtime, args[1].value, "RéponseServeurHTTP.envoyer_json", native_args.site),
                               {{"Content-Type", "application/json"}},
                               "RéponseServeurHTTP.envoyer_json",
                               native_args.site);
            return Value::rien();
        }));

    object->fields["envoyer_fichier"] = Value::fonction(make_native_function(
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 2, "RéponseServeurHTTP.envoyer_fichier", native_args.site);
            const int64_t status = stdlib_expect_integer(inner_runtime, args[0].value, "RéponseServeurHTTP.envoyer_fichier", native_args.site);
            const std::string path = stdlib_expect_text(inner_runtime, args[1].value, "RéponseServeurHTTP.envoyer_fichier", native_args.site);
            if (!std::filesystem::is_regular_file(path))
            {
                inner_runtime.raise_runtime_error(native_args.site, "RéponseServeurHTTP.envoyer_fichier requiert un fichier régulier");
            }
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                inner_runtime.raise_runtime_error(native_args.site, "RéponseServeurHTTP.envoyer_fichier a échoué: impossible d'ouvrir le fichier demandé");
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            send_http_response(inner_runtime,
                               state,
                               status,
                               buffer.str(),
                               {{"Content-Type", guess_content_type(path)}},
                               "RéponseServeurHTTP.envoyer_fichier",
                               native_args.site);
            return Value::rien();
        }));

    object->fields["rediriger"] = Value::fonction(make_native_function(
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(inner_runtime, args, 1, 2, "RéponseServeurHTTP.rediriger", native_args.site);
            const std::string url = stdlib_expect_text(inner_runtime, args[0].value, "RéponseServeurHTTP.rediriger", native_args.site);
            const int64_t status = args.size() == 2
                ? stdlib_expect_integer(inner_runtime, args[1].value, "RéponseServeurHTTP.rediriger", native_args.site)
                : 302;
            send_http_response(inner_runtime,
                               state,
                               status,
                               "",
                               {{"Location", url}},
                               "RéponseServeurHTTP.rediriger",
                               native_args.site);
            return Value::rien();
        }));

    Value result = Value::objet(std::move(object));
    runtime.annotate_value(result, "RéponseServeurHTTP", site);
    return result;
}

} // namespace lumiere
