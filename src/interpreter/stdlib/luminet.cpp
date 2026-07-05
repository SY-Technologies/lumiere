#include "lumiere/interpreter/stdlib/modules.hpp"

#if LUMIERE_ENABLE_LUMINET
#include "luminet_shared.hpp"
#endif

namespace lumiere
{

void register_luminet_module(Module &module, const NativeFunctionFactory &make_native_function)
{
#if LUMIERE_ENABLE_LUMINET
    auto root = make_hidden_typed_object("LumiNet");
    root->fields["Adresse"] = make_luminet_adresse_module(make_native_function);
    root->fields["DNS"] = make_luminet_dns_module(make_native_function);
    root->fields["TCP"] = make_luminet_tcp_module(make_native_function);
    root->fields["UDP"] = make_luminet_udp_module(make_native_function);
    root->fields["HTTP"] = make_luminet_http_module(make_native_function);
    root->fields["Canal"] = make_luminet_canal_module(make_native_function);

    stdlib_bind_public_value(module, "HTTP", root->fields["HTTP"]);
    stdlib_bind_public_value(module, "Canal", root->fields["Canal"]);
    stdlib_bind_public_value(module, "TCP", root->fields["TCP"]);
    stdlib_bind_public_value(module, "UDP", root->fields["UDP"]);
    stdlib_bind_public_value(module, "DNS", root->fields["DNS"]);
    stdlib_bind_public_value(module, "Adresse", root->fields["Adresse"]);
#else
    stdlib_bind_public_function(
        module,
        make_native_function,
        "indisponible",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            runtime.raise_runtime_error(native_args.site, "LumiNet n'est pas disponible sur cette plateforme");
        });
#endif
}

} // namespace lumiere
